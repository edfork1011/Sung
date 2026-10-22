import importlib.util
import pathlib
import unittest

spec=importlib.util.spec_from_file_location('catalog',pathlib.Path(__file__).parents[1]/'helper/catalog.py')
catalog=importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)

class CatalogTests(unittest.TestCase):
    def test_only_qq_music_can_handle_remote_requests(self):
        from unittest.mock import MagicMock, patch
        adapter=MagicMock();adapter.run.return_value={'items':[]}
        with patch.dict('sys.modules',{'qqmusic':adapter}):
            self.assertEqual(catalog.run({'op':'search','query':'music'}),{'items':[]})
            adapter.run.assert_called_once()
            for request in ({'op':'search','source':'unsupported'}, {'op':'external-artwork'}, {'op':'unknown'}):
                with self.assertRaises(ValueError):catalog.run(request)
            self.assertEqual(adapter.run.call_count,1)

    def test_home_always_uses_qq_music(self):
        from unittest.mock import MagicMock, patch
        adapter = MagicMock()
        adapter.run.return_value = {'sections': []}
        with patch.dict('sys.modules', {'qqmusic': adapter}):
            for request in ({'op': 'home'}, {'op': 'home', 'source': 'qqmusic'}):
                self.assertEqual(catalog.run(request), {'sections': []})
                adapter.run.assert_called_with(request)


    def test_local_metadata_embedded_art_and_missing_files(self):
        import subprocess, tempfile
        from urllib.parse import urlparse, unquote
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory);cover=root/'embedded-source.jpg';song=root/'Café #1.flac'
            def ff(*args): subprocess.run(['ffmpeg','-nostdin','-v','error',*args],check=True,capture_output=True,timeout=10)
            ff('-f','lavfi','-i','color=c=blue:s=32x32','-frames:v','1','-threads','1',str(cover))
            ff('-f','lavfi','-i','anullsrc=r=44100:cl=mono','-i',str(cover),'-map','0:a','-map','1:v','-c:a','flac','-c:v','copy','-disposition:v','attached_pic','-t','1','-metadata','title=Tagged title','-metadata','artist=Artist',str(song))
            alias=root/'alias.flac';alias.symlink_to(song)
            result=catalog.local_files({'files':[str(song),str(alias),str(root/'missing.mp3')],'artDirectory':str(root/'art')})
            self.assertEqual(len(result['items']),2)
            self.assertEqual(result['items'][0]['id'],result['items'][1]['id'])
            self.assertEqual(result['items'][0]['title'],'Tagged title')
            self.assertTrue(pathlib.Path(unquote(urlparse(result['items'][0]['art']).path)).is_file())
            self.assertEqual(result['failed'],['missing.mp3'])

    def test_folder_scan_nested_dedup_and_incremental(self):
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory); nested=root/'Album';nested.mkdir()
            song=nested/'Café.FLAC';song.write_bytes(b'fixture')
            (root/'ignore.txt').write_text('not audio')
            (root/'alias.flac').symlink_to(song)
            (nested/'loop').symlink_to(root, target_is_directory=True)
            result=catalog.scan_music_folders({'folders':[str(root),str(nested)]})
            self.assertEqual(result['files'],[str(song)])
            self.assertFalse(result['limited'])
            st=song.stat();known={str(song):f'{st.st_mtime_ns}:{st.st_size}'}
            self.assertEqual(catalog.scan_music_folders({'folders':[str(root)],'known':known})['files'],[])
            song.write_bytes(b'changed fixture')
            self.assertEqual(catalog.scan_music_folders({'folders':[str(root)],'known':known})['files'],[str(song)])
            self.assertEqual(catalog.scan_music_folders({'folders':[str(root/'absent')]})['failed'],1)

    def test_cleanup_exact_identity_and_missing_files(self):
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory);song=root/'Song.flac';song.write_bytes(b'audio')
            alias=root/'Alias.flac';alias.symlink_to(song)
            rows=[dict(id='a',source='qqmusic',remoteId='midA',title='Song'),dict(id='b',source='qqmusic',remoteId='midB',title='Song'),dict(id='c',source='qqmusic',remoteId='midA'),dict(localPath=str(song)),dict(localPath=str(alias)),dict(localPath=str(root/'Gone.mp3'))]
            issues=catalog.playlist_cleanup({'rows':rows})['issues']
            self.assertEqual(issues,[dict(index=2,duplicate=True,missing=False),dict(index=4,duplicate=True,missing=False),dict(index=5,duplicate=False,missing=True)])
            self.assertTrue(song.exists())


if __name__=='__main__':unittest.main()
