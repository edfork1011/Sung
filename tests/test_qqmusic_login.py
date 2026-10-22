import contextlib
import importlib.util
import io
import json
from pathlib import Path
import stat
import sys
import tempfile
from types import SimpleNamespace as NS
import unittest
from unittest.mock import AsyncMock, MagicMock, patch

HELPER = Path(__file__).parents[1] / 'helper'
spec = importlib.util.spec_from_file_location('login_adapter', HELPER / 'qqmusic.py')
adapter = importlib.util.module_from_spec(spec); spec.loader.exec_module(adapter)
spec = importlib.util.spec_from_file_location('qqmusic_login', HELPER / 'qqmusic_login.py')
login = importlib.util.module_from_spec(spec)
with patch.dict(sys.modules, {'qqmusic': adapter}):
    spec.loader.exec_module(login)
try:
    from qqmusic_api.models.login import QRCodeLoginEvents, QRLoginType
except ImportError:
    QRCodeLoginEvents = QRLoginType = None


class CredentialFileTests(unittest.TestCase):
    def test_snapshot_is_private_atomic_and_old_file_survives_serialization_error(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / 'credential.json'
            login.save_credential(NS(model_dump_json=lambda: '{"musicid":123,"musickey":"fixture-key"}'), target)
            original = target.read_text()
            self.assertEqual(stat.S_IMODE(target.stat().st_mode), 0o600)
            broken = NS(model_dump_json=MagicMock(side_effect=ValueError('fixture-error')))
            with self.assertRaises(ValueError): login.save_credential(broken, target)
            self.assertEqual(target.read_text(), original)
            self.assertEqual(list(Path(directory).iterdir()), [target])


@unittest.skipUnless(QRCodeLoginEvents, 'Install helper/requirements.txt for login tests')
class QRLoginTests(unittest.IsolatedAsyncioTestCase):
    async def test_app_protocol_streams_states_without_printing_credentials(self):
        from qqmusic_api import Credential
        credential = Credential(musicid=123456, musickey='private-fixture-key')
        client = MagicMock(); client.__aenter__ = AsyncMock(return_value=client); client.__aexit__ = AsyncMock(return_value=False)
        session = NS(get_qrcode=AsyncMock(return_value=NS(save=self.write_qr)))
        async def events():
            yield NS(event=QRCodeLoginEvents.SCAN)
            yield NS(event=QRCodeLoginEvents.CONF)
            yield NS(event=QRCodeLoginEvents.DONE, credential=credential)
        session.iter_events=events
        with tempfile.TemporaryDirectory() as directory:
            output=io.StringIO()
            with patch.object(login,'create_client',return_value=client), patch('qqmusic_api.modules.login_utils.QRCodeLoginSession',return_value=session), contextlib.redirect_stdout(output):
                result=await login.app_login({'directory':directory,'devicePath':str(Path(directory)/'device.json'),'loginType':'qq'})
            packets=[json.loads(line) for line in output.getvalue().splitlines()]
            self.assertEqual([packet['event'] for packet in packets],['qr','waiting','scanned'])
            self.assertNotIn('private-fixture-key',output.getvalue())
            self.assertNotIn('private-fixture-key',json.dumps(result))
            self.assertEqual(Credential.model_validate_json(Path(result['credentialFile']).read_text()).musicid,123456)

    async def test_app_expiry_and_refusal_do_not_overwrite_credentials(self):
        client = MagicMock(); client.__aenter__ = AsyncMock(return_value=client); client.__aexit__ = AsyncMock(return_value=False)
        for event in (QRCodeLoginEvents.TIMEOUT,QRCodeLoginEvents.REFUSE):
            session=NS(get_qrcode=AsyncMock(return_value=NS(save=self.write_qr)))
            async def events():
                yield NS(event=event)
            session.iter_events=events
            with tempfile.TemporaryDirectory() as directory:
                target=Path(directory)/'credential.json';target.write_text('old-snapshot')
                with patch.object(login,'create_client',return_value=client), patch('qqmusic_api.modules.login_utils.QRCodeLoginSession',return_value=session), contextlib.redirect_stdout(io.StringIO()):
                    result=await login.app_login({'directory':directory,'loginType':'wx'})
                self.assertFalse(result['ok'])
                self.assertEqual(target.read_text(),'old-snapshot')

    async def test_success_saves_importable_credentials_and_removes_qr(self):
        from qqmusic_api import Credential
        credential = Credential(musicid=123456, musickey='private-fixture-key')
        client = MagicMock(); client.__aenter__ = AsyncMock(return_value=client); client.__aexit__ = AsyncMock(return_value=False)
        qr = NS(save=lambda directory: self.write_qr(directory))
        session = NS(get_qrcode=AsyncMock(return_value=qr))
        async def events():
            yield NS(event=QRCodeLoginEvents.CONF)
            yield NS(event=QRCodeLoginEvents.DONE, credential=credential)
        session.iter_events = events
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory) / 'login'; device = Path(directory) / 'app/device.json'
            output = io.StringIO()
            with patch.object(login, 'create_client', return_value=client) as create, patch('qqmusic_api.modules.login_utils.QRCodeLoginSession', return_value=session) as make_session, contextlib.redirect_stdout(output):
                self.assertEqual(await login.login(folder, device, 'qq', False), 0)
            create.assert_called_once_with(device_path=str(device))
            make_session.assert_called_once_with(client.login, QRLoginType.QQ, timeout_seconds=180)
            saved = Credential.model_validate_json((folder / 'credential.json').read_text())
            self.assertEqual(saved.musicid, 123456)
            self.assertEqual(list(folder.iterdir()), [folder / 'credential.json'])
            self.assertNotIn('private-fixture-key', output.getvalue())

    async def test_timeout_and_refusal_leave_no_credential_or_qr(self):
        client = MagicMock(); client.__aenter__ = AsyncMock(return_value=client); client.__aexit__ = AsyncMock(return_value=False)
        for event in (QRCodeLoginEvents.TIMEOUT, QRCodeLoginEvents.REFUSE):
            session = NS(get_qrcode=AsyncMock(return_value=NS(save=lambda directory: self.write_qr(directory))))
            async def events():
                yield NS(event=event)
            session.iter_events = events
            with tempfile.TemporaryDirectory() as directory:
                folder = Path(directory) / 'login'
                with patch.object(login, 'create_client', return_value=client), patch('qqmusic_api.modules.login_utils.QRCodeLoginSession', return_value=session), contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(await login.login(folder, Path(directory) / 'device.json', 'wx', False), 1)
                self.assertEqual(list(folder.iterdir()), [])

    @staticmethod
    def write_qr(directory):
        image = Path(directory) / 'qr.png'; image.write_bytes(b'fixture'); return image
