"""Generate a Sung credential snapshot using QQMusicApi's QR login flow."""
import argparse
import asyncio
from contextlib import aclosing
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from qqmusic import create_client


def save_credential(credential, target):
    """Replace the snapshot atomically; never print the key or widen permissions."""
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=target.parent, delete=False) as file:
            temporary = Path(file.name)
            file.write(credential.model_dump_json())
        temporary.chmod(0o600)
        temporary.replace(target)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


async def login(folder, device_path, login_type='qq', open_image=True):
    from qqmusic_api.models.login import QRCodeLoginEvents, QRLoginType
    from qqmusic_api.modules.login_utils import QRCodeLoginSession
    folder.mkdir(parents=True, exist_ok=True, mode=0o700)
    device_path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    scanner = {'qq': '手机 QQ', 'wx': '微信', 'mobile': 'QQ 音乐手机客户端'}[login_type]
    async with create_client(device_path=str(device_path)) as client:
        session = QRCodeLoginSession(client.login, QRLoginType(login_type), timeout_seconds=180)
        qr = await asyncio.wait_for(session.get_qrcode(), timeout=35)
        with tempfile.TemporaryDirectory(prefix='qr-', dir=folder) as directory:
            image = qr.save(directory)
            if image is None:
                print('未获取到二维码图片，请重新运行。', file=sys.stderr)
                return 1
            print('请用%s扫码，并在手机上确认登录。' % scanner, flush=True)
            print('二维码：%s' % image, flush=True)
            if open_image:
                try:
                    subprocess.Popen(['xdg-open', str(image)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                except OSError:
                    print('无法自动打开图片，请手动打开上面的二维码文件。', flush=True)
            async with aclosing(session.iter_events()) as events:
                async for result in events:
                    if result.event == QRCodeLoginEvents.CONF:
                        print('已扫码，请在手机上确认。', flush=True)
                    elif result.event == QRCodeLoginEvents.DONE:
                        if result.credential is None:
                            print('登录响应缺少凭据，请重新运行。', file=sys.stderr)
                            return 1
                        target = folder / 'credential.json'
                        save_credential(result.credential, target)
                        print('凭据已保存：%s' % target, flush=True)
                        print('请在 Sung → Settings → Connections → Import QQ Music credentials 中选择此文件。', flush=True)
                        return 0
                    elif result.event == QRCodeLoginEvents.TIMEOUT:
                        print('二维码已过期，请重新运行。', file=sys.stderr)
                        return 1
                    elif result.event == QRCodeLoginEvents.REFUSE:
                        print('已取消登录。', file=sys.stderr)
                        return 1
    print('登录流程已结束，未获得凭据。', file=sys.stderr)
    return 1


async def app_login(req):
    """Stream only QR/state events; credentials stay in the private directory."""
    from qqmusic_api.models.login import QRCodeLoginEvents, QRLoginType
    from qqmusic_api.modules.login_utils import QRCodeLoginSession
    folder = Path(req['directory']).resolve(strict=True)
    login_type = QRLoginType(req.get('loginType', 'qq'))
    # QTemporaryDir is owned by the app and its process lifetime. No extra
    # directory is used here, so cancel/close removes the QR and pending login.
    def emit(event, **fields):
        print(json.dumps({'event': event, **fields}), flush=True)

    async with create_client(device_path=req.get('devicePath')) as client:
        session = QRCodeLoginSession(client.login, login_type, timeout_seconds=180)
        qr = await asyncio.wait_for(session.get_qrcode(), timeout=35)
        image = qr.save(folder)
        if image is None:
            return {'ok': False, 'error': 'QQ Music did not return a QR code. Try again.'}
        emit('qr', path=str(image))
        async with aclosing(session.iter_events()) as events:
            async for result in events:
                if result.event == QRCodeLoginEvents.SCAN:
                    emit('waiting')
                elif result.event == QRCodeLoginEvents.CONF:
                    emit('scanned')
                elif result.event == QRCodeLoginEvents.DONE:
                    if result.credential is None:
                        return {'ok': False, 'error': 'QQ Music login returned no credentials. Try again.'}
                    target = folder / 'credential.json'
                    save_credential(result.credential, target)
                    return {'credentialFile': str(target)}
                elif result.event == QRCodeLoginEvents.TIMEOUT:
                    emit('expired')
                    return {'ok': False, 'error': 'The QR code expired. Generate a new code to try again.'}
                elif result.event == QRCodeLoginEvents.REFUSE:
                    emit('refused')
                    return {'ok': False, 'error': 'Login was declined on your phone. You can try again.'}
    return {'ok': False, 'error': 'QQ Music login ended without credentials. Try again.'}


def run_app(req):
    os.umask(0o077)
    try:
        return asyncio.run(app_login(req))
    except TimeoutError:
        return {'ok': False, 'error': 'QQ Music connection timed out. Check your connection and try again.'}
    except Exception as exc:
        return {'ok': False, 'error': 'QQ Music login failed (%s). Try again or choose another login method.' % type(exc).__name__}


def main():
    parser = argparse.ArgumentParser(description='扫码获取可导入 Sung 的 QQ Music 凭据 JSON。')
    parser.add_argument('--type', choices=('qq', 'wx', 'mobile'), default='qq', help='扫码方式：qq（默认）、wx（微信）、mobile（QQ 音乐客户端）')
    parser.add_argument('--no-open', action='store_true', help='仅显示二维码文件路径，不自动打开图片')
    args = parser.parse_args()
    root = Path(os.environ['SUNG_QQMUSIC_ROOT']).resolve()
    state = root / '.dev-state/qqmusic'
    os.umask(0o077)
    try:
        return asyncio.run(login(state / 'login', state / 'data/Sung/sung/qqmusic-device.json', args.type, not args.no_open))
    except KeyboardInterrupt:
        print('\n已取消登录。', file=sys.stderr)
        return 130
    except TimeoutError:
        print('QQ Music 连接超时，请检查网络后重试。', file=sys.stderr)
        return 1
    except Exception as exc:
        # Upstream exceptions may contain login URLs or tokens.
        print('QQ Music 登录失败（%s）。请检查网络，或尝试 --type wx / --type mobile。' % type(exc).__name__, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
