"""Adapter tests; async contract tests use the pinned QQMusicApi dependency."""
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace as NS
import unittest
from unittest.mock import AsyncMock, MagicMock, patch

HELPER = Path(__file__).parents[1] / 'helper'
spec = importlib.util.spec_from_file_location('sung_qqmusic', HELPER / 'qqmusic.py')
qq = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qq)
try:
    import qqmusic_api
except ImportError:
    qqmusic_api = None


def song(mid='0027oMO61wWi55'):
    album = NS(title='Album', name='Album', mid='albumMID', cover_url=lambda size: 'https://art/%d.jpg' % size)
    return NS(mid=mid, id=123, title='Song', name='Song', singer=[NS(mid='artistMID', name='Artist')],
              album=album, interval=125, index_cd=2, status=0, type=1, file=NS(media_mid='mediaMID'))


class QQMusicTests(unittest.TestCase):
    def test_normalization_keeps_ids_sources_and_navigation_distinct(self):
        row = qq.normalize(song())
        self.assertEqual(row['id'], qq.item_id('song', row['remoteId']))
        self.assertNotEqual(row['id'], qq.item_id('album', row['remoteId']))
        self.assertNotIn('videoId', row)
        self.assertEqual((row['source'], row['artistId'], row['albumId']), ('qqmusic', 'artistMID', 'albumMID'))
        self.assertEqual((row['seconds'], row['duration'], row['discNumber']), (125, '2:05', 2))
        self.assertEqual((row['mediaMid'], row['songType'], row['qqMusicSong']), ('mediaMID', 1, True))
        highlighted = song(); highlighted.title = '<em>Song</em> &amp; Artist'
        self.assertEqual(qq.normalize(highlighted)['title'], 'Song & Artist')
        unavailable = song(); unavailable.status = 1
        self.assertFalse(qq.normalize(unavailable)['available'])

    def test_supported_links_and_rejected_hosts(self):
        cases = [('https://y.qq.com/n/ryqq/songDetail/abcMID', 'song', 'abcMID'),
                 ('https://y.qq.com/n/ryqq/albumDetail/albumMID', 'album', 'albumMID'),
                 ('https://y.qq.com/n/ryqq/singer/artistMID', 'artist', 'artistMID'),
                 ('https://y.qq.com/n/ryqq/playlist/12345', 'playlist', '12345'),
                 ('https://y.qq.com/n/yqq/song/abcMID.html', 'song', 'abcMID'),
                 ('https://i.y.qq.com/v8/playsong.html?songmid=abcMID', 'song', 'abcMID'),
                 ('https://y.qq.com/n/yqq/playlist/123.html?id=123', 'playlist', '123')]
        for url, op, value in cases:
            with self.subTest(url=url):
                self.assertEqual(qq.link_request(url), {'op': op, 'id': value})
        for url in ['https://y.qq.com.evil.test/song/abc', 'file:///song/abc', 'https://y.qq.com/', 'https://y.qq.com/n/ryqq/playlist/notnumeric']:
            with self.assertRaises(qq.QQMusicError): qq.link_request(url)
        for value in ('../escape', '', 'a' * 65, 'with space'):
            with self.assertRaises(qq.QQMusicError): qq.remote_id(value)

    def test_buffer_limit_and_empty_download(self):
        with tempfile.TemporaryDirectory() as directory:
            response = MagicMock(); response.__enter__.return_value = response
            response.headers = {'Content-Length': '4'}; response.read.side_effect = [b'mp3!', b'']
            with patch('urllib.request.urlopen', return_value=response):
                result = qq.buffer_audio({'url': 'https://audio.test/song'}, directory)
            self.assertEqual(Path(result['file']).read_bytes(), b'mp3!')
            Path(result['file']).unlink()
            response.headers = {'Content-Length': str(33 * 1024 * 1024)}
            with patch('urllib.request.urlopen', return_value=response):
                self.assertIn('url', qq.buffer_audio({'url': 'https://audio.test/song'}, directory))
            response.headers = {}; response.read.side_effect = [b'']
            with patch('urllib.request.urlopen', return_value=response):
                with self.assertRaises(qq.QQMusicError): qq.buffer_audio({'url': 'https://audio.test/song'}, directory)
            self.assertEqual(list(Path(directory).iterdir()), [])


@unittest.skipUnless(qqmusic_api, 'Install helper/requirements.txt for QQMusicApi contract tests')
class QQMusicApiTests(unittest.IsolatedAsyncioTestCase):
    def account_client(self):
        client = MagicMock()
        client.credential = qqmusic_api.Credential(musicid=111, musickey='fixture-key', encryptUin='encoded-identity')
        return client

    async def test_account_liked_songs_use_authenticated_identity_and_stable_pages(self):
        client = self.account_client()
        def page(euin, *, num, page):
            return NS(code=0, subcode=0, total=205, songs=[song('liked%d' % i) for i in range((page-1)*num,min(page*num,205))])
        client.user.get_fav_song = AsyncMock(side_effect=page)
        result = await qq.dispatch({'op':'account','mode':'favorites','limit':205,'uin':'wrong-account'},client)
        self.assertEqual(len(result['items']),205)
        self.assertFalse(result['more'])
        self.assertTrue(all(call.args==('encoded-identity',) and call.kwargs['num']==100 for call in client.user.get_fav_song.call_args_list))
        self.assertEqual(sorted(c.kwargs['page'] for c in client.user.get_fav_song.call_args_list),[1,2,3])
        self.assertTrue(all(row['source']=='qqmusic' and row['qqMusicId']==123 for row in result['items']))
        limited = await qq.dispatch({'op':'account','mode':'favorites','limit':100},client)
        self.assertEqual(len(limited['items']),100); self.assertTrue(limited['more'])
        client.user.get_fav_song.return_value = NS(code=0,subcode=1,total=0,songs=[])
        client.user.get_fav_song.side_effect = None
        with self.assertRaises(qq.QQMusicError): await qq.account_library({'mode':'favorites'},client)

    async def test_account_playlists_merge_created_collected_and_exclude_virtual_folders(self):
        client = self.account_client()
        def playlist(identity,dirid=1):
            return NS(id=identity,dirid=dirid,title='Playlist',picurl='https://cover.test/p.jpg',songnum=10,nick='Owner',invalid=False)
        client.user.get_created_songlist = AsyncMock(return_value=NS(playlists=[playlist(0),playlist(1,201),playlist(123)],finished=True))
        client.user.get_fav_songlist = AsyncMock(return_value=NS(playlists=[playlist(123),playlist(234)],total=2,hide=False,failed_ids=[]))
        result=await qq.account_library({'mode':'playlists'},client)
        self.assertEqual([p['remoteId'] for p in result['items']],['123','234'])
        self.assertEqual([p['playlistOrigin'] for p in result['items']],['Created','Collected'])
        self.assertEqual([p['owned'] for p in result['items']],[True,False])
        self.assertTrue(all(p['kind']=='playlist' and p['count']==10 for p in result['items']))
        self.assertFalse(result['notice'])
        client.user.get_created_songlist.assert_awaited_once_with(111)
        client.user.get_fav_songlist.side_effect=OSError('private-key')
        result=await qq.account_library({'mode':'playlists'},client)
        self.assertEqual(len(result['items']),1);self.assertTrue(result['notice'])
        self.assertNotIn('private-key',str(result))
        client.user.get_created_songlist.side_effect=OSError('private-key')
        with self.assertRaises(qq.QQMusicError):await qq.account_library({'mode':'playlists'},client)

    async def test_account_concurrency_accepts_unhashable_cgi_requests(self):
        class Request:
            __hash__ = None
            def __init__(self,result):self.result=result
            def __await__(self):
                async def value():return self.result
                return value().__await__()
        client=self.account_client()
        client.user.get_created_songlist.return_value=Request(NS(playlists=[],finished=True))
        client.user.get_fav_songlist.return_value=Request(NS(playlists=[],total=0,hide=False,failed_ids=[]))
        result=await qq.account_library({'mode':'playlists'},client)
        self.assertEqual(result['items'],[]);self.assertFalse(result['notice'])

    async def test_collected_playlists_page_without_truncating_or_hiding_failures(self):
        client=self.account_client()
        client.user.get_created_songlist=AsyncMock(return_value=NS(playlists=[],finished=True))
        def page(euin,*,num,page):
            rows=[NS(id=i+1,dirid=1,title='Playlist',songnum=1,picurl='') for i in range((page-1)*100,min(page*100,105))]
            return NS(playlists=rows,total=105,hide=False,failed_ids=[])
        client.user.get_fav_songlist=AsyncMock(side_effect=page)
        result=await qq.account_library({'mode':'playlists','limit':105},client)
        self.assertEqual(len(result['items']),105);self.assertFalse(result['more'])
        self.assertEqual(sorted(c.kwargs['page'] for c in client.user.get_fav_songlist.call_args_list),[1,2])
        async def incomplete(euin,*,num,page):
            if page==2:raise OSError('private-key')
            return NS(playlists=[NS(id=i+1,dirid=1,title='Playlist',songnum=1,picurl='') for i in range(100)],total=105,hide=False,failed_ids=[])
        client.user.get_fav_songlist.side_effect=incomplete
        result=await qq.account_library({'mode':'playlists','limit':105},client)
        self.assertEqual(len(result['items']),100);self.assertTrue(result['notice'])

    async def test_account_requires_login_and_full_identity(self):
        client=self.account_client();client.credential=qqmusic_api.Credential()
        with self.assertRaisesRegex(qq.QQMusicError,'Sign in'):await qq.account_library({'mode':'favorites'},client)
        client.credential=qqmusic_api.Credential(musicid=111,musickey='fixture-key')
        with self.assertRaisesRegex(qq.QQMusicError,'account identifier'):await qq.account_library({'mode':'playlists'},client)
        client.user.get_fav_song.assert_not_called()

    async def test_like_writes_numeric_song_id_and_preserves_failed_state(self):
        client=self.account_client();client.songlist.like_song=AsyncMock(return_value=True);client.songlist.unlike_song=AsyncMock(return_value=False)
        request={'op':'like','id':'songMID','qqMusicId':123,'songType':1,'liked':True}
        self.assertEqual(await qq.dispatch(request,client),{'liked':True})
        client.songlist.like_song.assert_awaited_once_with([(123,1)])
        with self.assertRaisesRegex(qq.QQMusicError,'did not save'):await qq.dispatch({**request,'liked':False},client)
        client.songlist.unlike_song.assert_awaited_once_with([(123,1)])
        client.song.query_song=AsyncMock(return_value=NS(tracks=[song()]))
        await qq.dispatch({**request,'qqMusicId':None},client)
        self.assertEqual(client.song.query_song.call_args.args[0][0].mid,'songMID')

    async def test_recommendations_exclude_the_seed_song(self):
        client=self.account_client()
        client.recommend.get_guess_recommend=AsyncMock(return_value=NS(songs=[song('seedMID'),song('nextMID')]))
        result=await qq.dispatch({'op':'radio','id':'seedMID'},client)
        self.assertEqual([item['remoteId'] for item in result['items']],['nextMID'])
        self.assertEqual(result['items'][0]['source'],'qqmusic')

    def recommendation_client(self):
        return NS(recommend=NS(
            get_guess_recommend=AsyncMock(return_value=NS(songs=[song()])),
            get_recommend_songlist=AsyncMock(return_value=NS(songlists=[NS(id=123, title='Playlist', creator_nick='Curator', picurl='https://cover.test/list')])),
            get_recommend_newsong=AsyncMock(return_value=NS(songs=[song('newSong')])),
        ), album=NS(get_new_album=AsyncMock(return_value=NS(albums=[NS(mid='newAlbum', name='Album', singer_list=song().singer, picurl='https://cover.test/album')]))))

    async def test_home_cards_use_qq_ids_and_supported_navigation_targets(self):
        client = self.recommendation_client()
        client.recommend.get_recommend_newsong.return_value.songs = [song('new%d' % i) for i in range(40)]
        result = await qq.dispatch({'op': 'home'}, client)
        self.assertEqual([s['title'] for s in result['sections']], ['Made for you', 'Recommended playlists', 'New songs', 'New albums'])
        self.assertEqual([len(s['items']) for s in result['sections']], [1, 1, 18, 1])
        self.assertFalse(result['notice'])
        for section in result['sections']:
            for row in section['items']:
                self.assertEqual(row['source'], 'qqmusic')
                self.assertNotIn('videoId', row)
                self.assertEqual(row['id'], qq.item_id(row['kind'], row['remoteId']))
        playlist = result['sections'][1]['items'][0]
        self.assertEqual((playlist['kind'], playlist['browseId'], playlist['artist']), ('playlist', '123', 'Curator'))
        album = result['sections'][3]['items'][0]
        self.assertEqual((album['kind'], album['browseId'], album['artist']), ('album', 'newAlbum', 'Artist'))

    async def test_home_partial_failure_keeps_playable_shelves_and_hides_upstream_errors(self):
        client = self.recommendation_client()
        client.recommend.get_guess_recommend.side_effect = OSError('musickey=private-fixture')
        client.recommend.get_recommend_newsong.return_value.songs = [NS(mid='bad MID'), song(), song()]
        result = await qq.home(client)
        self.assertEqual(len(result['sections']), 3)
        self.assertEqual(len(result['sections'][1]['items']), 1)
        self.assertTrue(result['notice'])
        self.assertNotIn('private-fixture', json.dumps(result))

    async def test_home_all_empty_or_failed_reports_retryable_error(self):
        client = self.recommendation_client()
        client.recommend.get_guess_recommend.return_value.songs = []
        client.recommend.get_recommend_songlist.side_effect = TimeoutError('private-fixture')
        client.recommend.get_recommend_newsong.return_value.songs = []
        client.album.get_new_album.side_effect = OSError('private-fixture')
        with self.assertRaisesRegex(qq.QQMusicError, 'recommendations could not be loaded') as caught:
            await qq.home(client)
        self.assertNotIn('private-fixture', str(caught.exception))

    async def test_empty_and_duplicate_cookie_names_survive_payload_snapshot(self):
        from niquests import Response
        from niquests.cookies import RequestsCookieJar
        from qqmusic_api.core.response import snapshot_payload
        jar = RequestsCookieJar()
        jar.set('ETK', '', domain='.qq.com', path='/')
        jar.set('qrsig', 'qr-token', domain='.qq.com', path='/')
        jar.set('duplicate', 'first', domain='one.qq.com', path='/')
        jar.set('duplicate', 'last', domain='two.qq.com', path='/')
        response = Response(); response.status_code = 200; response.url = 'https://ssl.ptlogin2.qq.com/ptqrlogin'
        response._content = b"ptuiCB('66')"; response.encoding = 'utf-8'; response.cookies = jar
        # This is the original library failure reported during QQ QR polling.
        with self.assertRaises(KeyError): snapshot_payload(response)
        data = snapshot_payload(qq.CookieResponse(response))
        self.assertEqual(dict(data.cookies), {'ETK': '', 'qrsig': 'qr-token', 'duplicate': 'last'})
        self.assertEqual((data.status_code, data.content, data.url), (200, response.content, response.url))
        self.assertEqual(jar.get_dict()['ETK'], '')

    async def test_transport_normalizes_single_and_batch_without_losing_errors(self):
        response = NS(cookies={'ETK': '', 'none': None, 'qrsig': 'qr-token'}, status_code=200)
        failure = OSError('network failed')
        inner = NS(request=AsyncMock(return_value=response), request_many=AsyncMock(return_value=[response, failure]), close=AsyncMock())
        transport = qq.CookieSafeTransport(inner)
        single = await transport.request('single')
        self.assertEqual(single.cookies, {'ETK': '', 'none': '', 'qrsig': 'qr-token'})
        self.assertEqual(single.status_code, 200)
        result = await transport.request_many(['first', 'second'], return_exceptions=True)
        self.assertEqual(result[0].cookies, single.cookies)
        self.assertIs(result[1], failure)
        inner.request_many.assert_awaited_once_with(['first', 'second'], return_exceptions=True)
        await transport.close(); inner.close.assert_awaited_once()

    async def test_search_uses_stable_pages_and_correct_filters(self):
        client = MagicMock()
        client.search.search_by_type = AsyncMock(side_effect=[NS(song=[song('first%d' % i) for i in range(30)], nextpage=2),
                                                             NS(song=[song('next%d' % i) for i in range(5)], nextpage=-1)])
        result = await qq.dispatch({'op': 'search', 'query': 'Artist', 'filter': 'songs', 'limit': 60}, client)
        self.assertEqual(len(result['items']), 35)
        self.assertFalse(result['more'])
        self.assertEqual([call.kwargs['page'] for call in client.search.search_by_type.call_args_list], [1, 2])
        self.assertTrue(all(call.kwargs['num'] == 30 for call in client.search.search_by_type.call_args_list))
        for selected, field, kind in [('albums', 'album', 'album'), ('artists', 'singer', 'artist'), ('playlists', 'songlist', 'playlist')]:
            item = NS(id=123, mid='testMID', title='Result', picurl='https://cover.test/a')
            client.search.search_by_type = AsyncMock(return_value=NS(**{field: [item], 'nextpage': -1}))
            result = await qq.dispatch({'op': 'search', 'query': 'Artist', 'filter': selected}, client)
            self.assertEqual(result['items'][0]['kind'], kind)
        with self.assertRaises(qq.QQMusicError):
            await qq.dispatch({'op': 'search', 'query': 'Artist', 'filter': 'videos'}, client)

    async def test_album_and_playlist_collect_pages(self):
        client = MagicMock()
        client.album.get_detail = AsyncMock(return_value=NS(album=song().album, singers=song().singer))
        client.album.get_detail.return_value.album.time_public = '2026-01-01'
        client.album.get_song = AsyncMock(side_effect=[NS(song_list=[song('song%d' % i) for i in range(100)], total_num=102), NS(song_list=[song('last1'), song('last2')], total_num=102)])
        result = await qq.dispatch({'op': 'album', 'id': 'albumMID', 'limit': 200}, client)
        self.assertEqual((len(result['items']), result['year'], result['more']), (102, '2026', False))
        info = NS(title='Playlist', picurl='https://cover.test/a')
        client.songlist.get_detail = AsyncMock(return_value=NS(info=info, total=2, songs=[song(), song('second')], hasmore=0))
        result = await qq.dispatch({'op': 'playlist', 'id': '123', 'limit': 100}, client)
        self.assertEqual((result['title'], len(result['items']), result['more']), ('Playlist', 2, False))
        self.assertEqual(client.songlist.get_detail.call_args.args, (123,))

    async def test_authorized_url_and_denied_permissions(self):
        client = MagicMock()
        client.song.get_song_urls = AsyncMock(return_value=NS(data=[NS(result=0, purl='M500song.mp3?vkey=test')]))
        client.song.get_cdn_dispatch = AsyncMock(return_value=NS(sip=['http://audio.test/']))
        result = await qq.dispatch({'op': 'resolve', 'id': 'songMID', 'mediaMid': 'mediaMID', 'songType': 1}, client)
        self.assertEqual(result['url'], 'https://audio.test/M500song.mp3?vkey=test')
        request = client.song.get_song_urls.call_args.args[0][0]
        self.assertEqual((request.mid, request.media_mid, request.song_type), ('songMID', 'mediaMID', 1))
        client.song.get_song_urls.return_value = NS(data=[NS(result=104003, purl='')])
        with self.assertRaisesRegex(qq.QQMusicError, 'did not authorize'):
            await qq.dispatch({'op': 'resolve', 'id': 'songMID'}, client)
        self.assertEqual(client.song.get_song_urls.await_count, 2)

    async def test_lyric_contract_uses_mid_and_lrc(self):
        client = MagicMock(); client.lyric.get_lyric = AsyncMock(return_value=NS(lyric='[00:01.00] One &amp; two'))
        result = await qq.dispatch({'op': 'lyrics', 'id': 'songMID'}, client)
        client.lyric.get_lyric.assert_awaited_once_with('songMID', qrc=False)
        self.assertEqual((result['source'], result['lrc']), ('QQ Music', '[00:01.00] One & two'))


@unittest.skipUnless(qqmusic_api, 'Install helper/requirements.txt for QQMusicApi contract tests')
class QQMusicCredentialTests(unittest.TestCase):
    def test_credentials_and_upstream_errors_do_not_expose_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'credential.json'
            path.write_text(json.dumps({'musicid': 123, 'musickey': 'secret-fixture'}))
            credential = qq.load_credential(path, qqmusic_api.Credential)
            self.assertEqual(credential.musicid, 123)
            path.write_text(json.dumps({'musicid': 'bad-secret-input', 'musickey': 'secret-fixture'}))
            with self.assertRaises(qq.QQMusicError) as error: qq.load_credential(path, qqmusic_api.Credential)
            self.assertNotIn('secret', str(error.exception))
        client = MagicMock(); client.__aenter__ = AsyncMock(return_value=client); client.__aexit__ = AsyncMock(return_value=False)
        with patch.object(qqmusic_api, 'Client', return_value=client), patch.object(qq, 'dispatch', AsyncMock(side_effect=ValueError('musickey=secret-fixture'))):
            with self.assertRaises(RuntimeError) as error:
                qq.run({'op': 'search', 'query': 'test'})
        self.assertNotIn('secret-fixture', str(error.exception))


if __name__ == '__main__':
    unittest.main()
