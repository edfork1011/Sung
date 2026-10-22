"""QQMusicApi adapter for Sung's one-request-per-process helper protocol."""
import asyncio
import hashlib
import html
import json
import re
from pathlib import Path
from urllib.parse import parse_qs, urljoin, urlparse


class QQMusicError(ValueError):
    """Safe, user-facing errors; upstream validation errors must not be echoed."""


class CookieResponse:
    """Expose a real mapping at QQMusicApi's RawResponse boundary.

    niquests cookie jars raise KeyError for empty values, including ETK sent
    by QQ login, and reject duplicate names across domains. QQMusicApi 0.8.2
    snapshots cookies by name. get_dict() reads cookie records directly and
    already defines how a jar is flattened to the mapping the API expects.
    """

    def __init__(self, response):
        self.response = response
        jar = response.cookies
        values = jar.get_dict() if hasattr(jar, 'get_dict') else dict(jar)
        self.cookies = {name: value if value is not None else '' for name, value in values.items()}

    def __getattr__(self, name):
        return getattr(self.response, name)


class CookieSafeTransport:
    """Adapt buffered responses without changing niquests or installed packages."""

    def __init__(self, transport=None):
        if transport is None:
            from qqmusic_api.core.transport import NiquestsTransport
            transport = NiquestsTransport(timeout=(10, 20))
        self.transport = transport

    async def request(self, request):
        return CookieResponse(await self.transport.request(request))

    async def request_many(self, requests, *, return_exceptions=True):
        outcomes = await self.transport.request_many(requests, return_exceptions=return_exceptions)
        return [result if isinstance(result, Exception) else CookieResponse(result) for result in outcomes]

    async def close(self):
        await self.transport.close()


def create_client(**kwargs):
    from qqmusic_api import Client
    return Client(transport=CookieSafeTransport(), **kwargs)


def remote_id(value, kind='song'):
    value = str(value or '')
    if not re.fullmatch(r'[A-Za-z0-9]{1,64}', value):
        raise QQMusicError('Invalid QQ Music %s ID' % kind)
    if kind == 'playlist' and not value.isdecimal():
        raise QQMusicError('QQ Music playlist ID must be numeric')
    return value


def item_id(kind, value):
    return 'qq_' + hashlib.sha256((kind + ':' + str(value)).encode()).hexdigest()


def plain_text(value):
    # The search service sometimes returns highlight markup even with it disabled.
    return html.unescape(re.sub(r'</?(?:em|font)\b[^>]*>', '', value, flags=re.IGNORECASE))


def normalize(item, kind='song'):
    """Normalize QQMusicApi 0.8.2 models using provider-specific song identities."""
    value = remote_id(item.id if kind == 'playlist' else item.mid, kind)
    singers = getattr(item, 'singer', None)
    if not isinstance(singers, list):
        singers = getattr(item, 'singer_list', None) or getattr(item, 'singers', [])
    album = getattr(item, 'album', None)
    seconds = max(0, int(getattr(item, 'interval', 0)))
    cover = getattr(item, 'cover_url', None)
    art = (album.cover_url(800) if album else cover(800) if cover else '')
    art = art or getattr(item, 'picurl', '') or getattr(item, 'pic', '')
    if art.startswith('http://'):
        art = 'https://' + art[7:]
    result = {
        'id': item_id(kind, value), 'source': 'qqmusic', 'remoteId': value,
        'browseId': value if kind != 'song' else '', 'kind': kind,
        'title': plain_text(getattr(item, 'title', '') or getattr(item, 'name', '') or 'Untitled'),
        'artist': ', '.join(s.name or s.title for s in singers) or getattr(item, 'singer_name', '') or getattr(item, 'creator_nick', '') or getattr(item, 'nickname', '') or getattr(item, 'nick', ''),
        'artistId': next((s.mid for s in singers if s.mid), ''),
        'album': (album.title or album.name) if album else '',
        'albumId': album.mid if album else '', 'art': art,
        'seconds': seconds, 'duration': '%d:%02d' % divmod(seconds, 60) if seconds else '',
        'discNumber': max(1, int(getattr(item, 'index_cd', 1))),
        'available': getattr(item, 'status', 0) not in (1, 2), 'qqMusicSong': kind == 'song',
    }
    if kind == 'song':
        result['qqMusicId'] = item.id
        result['mediaMid'] = item.file.media_mid
        result['songType'] = item.type
    return result


def link_request(url):
    parsed = urlparse(url)
    if parsed.scheme not in ('https', 'http') or parsed.hostname not in ('y.qq.com', 'www.y.qq.com', 'i.y.qq.com'):
        raise QQMusicError('Paste a QQ Music song, album, artist or playlist link')
    query = parse_qs(parsed.query)
    for key, op in [('songmid', 'song'), ('albummid', 'album'), ('singermid', 'artist'), ('disstid', 'playlist'), ('id', 'song')]:
        if query.get(key):
            # Legacy playlist pages also use ?id= rather than ?disstid=.
            if key == 'id' and ('playlist' in parsed.path or 'taoge' in parsed.path):
                op = 'playlist'
            return {'op': op, 'id': remote_id(query[key][0], op)}
    match = re.search(r'/(songDetail|song|albumDetail|album|singer|playlist|taoge)/([A-Za-z0-9]+)(?:\.html)?/?$', parsed.path)
    if match:
        op = {'songDetail': 'song', 'albumDetail': 'album', 'singer': 'artist', 'taoge': 'playlist'}.get(match[1], match[1])
        return {'op': op, 'id': remote_id(match[2], op)}
    raise QQMusicError('This QQ Music link has no supported song, album, artist or playlist ID')


def load_credential(path, credential_class):
    if not path:
        return credential_class()
    try:
        with open(path, 'rb') as file:
            payload = file.read(65537)
        if len(payload) > 65536:
            raise ValueError()
        data = json.loads(payload)
        credential = credential_class.model_validate(data)
        if credential.musicid <= 0 or not credential.musickey:
            raise ValueError()
        return credential
    except Exception:
        # Pydantic validation errors can echo the input, including credentials.
        raise QQMusicError('Invalid QQ Music credential JSON. Import a Credential snapshot with musicid and musickey.') from None


async def dispatch(req, client):
    from qqmusic_api.modules.song import SongFileInfo, SongQueryInfo
    op = req.get('op', '')
    if op == 'home':
        return await home(client)
    if op == 'radio':
        response = await client.recommend.get_guess_recommend()
        return {'items': [normalize(song) for song in response.songs if song.mid != req.get('id')]}
    if op == 'account':
        return await account_library(req, client)
    if op == 'like':
        account_credential(client)
        value = remote_id(req.get('id'))
        if not isinstance(req.get('liked'), bool):
            raise QQMusicError('Invalid QQ Music favorite action')
        song_id = int(req.get('qqMusicId') or 0)
        song_type = int(req.get('songType') or 0)
        if song_id <= 0:
            data = await client.song.query_song([SongQueryInfo(mid=value)])
            if not data.tracks:
                raise QQMusicError('This QQ Music song is unavailable')
            song_id, song_type = data.tracks[0].id, data.tracks[0].type
        action = client.songlist.like_song if req['liked'] else client.songlist.unlike_song
        if not await action([(song_id, song_type)]):
            raise QQMusicError('QQ Music did not save the favorite change. Refresh and try again.')
        return {'liked': req['liked']}
    limit = min(max(int(req.get('limit', 30)), 1), 200 if op == 'search' else 5000)
    if op == 'link':
        return await dispatch({**req, **link_request(req['url'])}, client)
    if op == 'search':
        from qqmusic_api.modules.search import SearchType
        filters = {'songs': (SearchType.SONG, 'song', 'song'),
                   'albums': (SearchType.ALBUM, 'album', 'album'),
                   'artists': (SearchType.SINGER, 'singer', 'artist'),
                   'playlists': (SearchType.SONGLIST, 'songlist', 'playlist')}
        selected = req.get('filter') or 'songs'
        if selected not in filters:
            raise QQMusicError('QQ Music supports songs, albums, artists and playlists')
        search_type, field, kind = filters[selected]
        rows, more = [], False
        # Sung asks for a growing prefix. Keep the upstream page size stable.
        for page in range(1, (limit + 29) // 30 + 1):
            data = await client.search.search_by_type(req['query'], search_type=search_type, num=30, page=page, highlight=False)
            incoming = getattr(data, field)
            rows.extend(normalize(item, kind) for item in incoming)
            more = data.nextpage != -1 and bool(incoming)
            if not more:
                break
        return {'items': rows[:limit], 'more': len(rows) > limit or more}
    value = remote_id(req.get('id'), op)
    if op == 'song':
        from qqmusic_api.modules.song import SongQueryInfo
        data = await client.song.query_song([SongQueryInfo(id=int(value)) if value.isdecimal() else SongQueryInfo(mid=value)])
        rows = [normalize(item) for item in data.tracks]
        if not rows:
            raise QQMusicError('This QQ Music song is unavailable')
        return {'title': rows[0]['title'], 'items': rows}
    if op == 'album':
        detail = await client.album.get_detail(value)
        rows, total = [], 0
        for page in range(1, (limit + 99) // 100 + 1):
            data = await client.album.get_song(value, num=100, page=page)
            rows.extend(normalize(item) for item in data.song_list)
            total = data.total_num
            if len(rows) >= total or not data.song_list:
                break
        return {'title': detail.album.title or detail.album.name, 'art': detail.album.cover_url(800),
                'artist': ', '.join(s.name for s in detail.singers), 'year': detail.album.time_public[:4],
                'items': rows[:limit], 'total': total, 'more': len(rows) > limit or len(rows) < total}
    if op == 'playlist':
        rows, total, info = [], 0, None
        for page in range(1, (limit + 99) // 100 + 1):
            data = await client.songlist.get_detail(int(value), num=100, page=page)
            info, total = data.info, data.total
            rows.extend(normalize(item) for item in data.songs)
            if not data.hasmore or not data.songs:
                break
        return {'title': info.title, 'art': info.picurl, 'items': rows[:limit], 'total': total,
                'more': len(rows) > limit or len(rows) < total}
    if op == 'artist':
        songs = await client.singer.get_songs_list(value, num=30)
        albums = await client.singer.get_album_list(value, num=30)
        return {'title': req.get('title') or 'QQ Music artist',
                'art': 'https://y.gtimg.cn/music/photo_new/T001R800x800M000%s.jpg' % value,
                'sections': [{'title': 'Songs', 'items': [normalize(item) for item in songs.song_list]},
                             {'title': 'Albums', 'items': [normalize(item, 'album') for item in albums.album_list]}]}
    if op == 'lyrics':
        data = await client.lyric.get_lyric(value, qrc=False)
        return {'lrc': html.unescape(data.lyric), 'lyrics': html.unescape(data.lyric), 'source': 'QQ Music'}
    if op in ('resolve', 'buffer'):
        urls = await client.song.get_song_urls([SongFileInfo(mid=value, media_mid=req.get('mediaMid') or None,
                                                            song_type=req.get('songType', 0))])
        info = next((item for item in urls.data if item.result == 0 and item.purl), None)
        if not info:
            raise QQMusicError('QQ Music did not authorize this track. Import or renew your QQ Music credentials in Settings and check your subscription or regional access.')
        cdn = await client.song.get_cdn_dispatch()
        base = next((url for url in cdn.sip if url.startswith(('http://', 'https://'))), '')
        url = urljoin(base, info.purl)
        if url.startswith('http://'):
            url = 'https://' + url[7:]
        if urlparse(url).scheme != 'https' or not urlparse(url).hostname:
            raise QQMusicError('QQ Music returned an invalid audio URL')
        return {'url': url}
    raise QQMusicError('Unsupported QQ Music request')


def account_credential(client, encrypted=False):
    credential = client.credential
    if not credential.is_valid():
        raise QQMusicError('Sign in to QQ Music to load your library.')
    if encrypted and not credential.encrypt_uin:
        raise QQMusicError('Sign in to QQ Music again to obtain the account identifier needed for your library.')
    return credential


async def account_library(req, client):
    """Use the authenticated identity, never an account ID supplied by the UI."""
    credential = account_credential(client, encrypted=True)
    limit = min(max(int(req.get('limit', 100)), 1), 5000)
    mode = req.get('mode')
    semaphore = asyncio.Semaphore(4)

    async def bounded(request):
        async with semaphore:
            return await request

    def check_songs(data):
        if data.code or data.subcode:
            raise QQMusicError('QQ Music could not load your liked songs. Sign in again or refresh to try again.')

    if mode == 'favorites':
        first = await client.user.get_fav_song(credential.encrypt_uin, num=100, page=1)
        check_songs(first)
        if first.total and not first.songs:
            raise QQMusicError('QQ Music did not return your liked songs. Refresh or sign in again to try again.')
        page_count = (min(limit, first.total) + 99) // 100
        pages = await asyncio.gather(*(bounded(client.user.get_fav_song(credential.encrypt_uin, num=100, page=p)) for p in range(2, page_count+1)))
        items, seen = [], set()
        for data in [first, *pages]:
            check_songs(data)
            for song in data.songs:
                row = normalize(song)
                if row['id'] not in seen:
                    items.append(row)
                    seen.add(row['id'])
        return {'title': 'QQ Music · Liked songs', 'items': items[:limit], 'total': first.total,
                'more': first.total > limit,
                'notice': 'Showing the first 5,000 liked songs.' if first.total > 5000 else ''}

    if mode != 'playlists':
        raise QQMusicError('Unsupported QQ Music library page')
    created, collected = await asyncio.gather(
        bounded(client.user.get_created_songlist(credential.musicid)),
        bounded(client.user.get_fav_songlist(credential.encrypt_uin, num=100, page=1)),
        return_exceptions=True,
    )
    if isinstance(created, Exception) and isinstance(collected, Exception):
        raise QQMusicError('QQ Music could not load your playlists. Sign in again or refresh to try again.')
    items, seen = [], set()
    incomplete = False

    def append(playlists, owned):
        for playlist in playlists:
            # 201 is "Liked songs"; virtual folders with no disstid are not playlists.
            if playlist.id <= 0 or playlist.dirid == 201 or getattr(playlist, 'invalid', False):
                continue
            row = normalize(playlist, 'playlist')
            if row['id'] in seen:
                continue
            seen.add(row['id'])
            row.update(count=playlist.songnum, owned=owned,
                       playlistOrigin='Created' if owned else 'Collected')
            items.append(row)

    if isinstance(created, Exception):
        incomplete = True
    else:
        append(created.playlists, True)
        incomplete = not created.finished
    created_count = len(items)
    total = created_count
    if isinstance(collected, Exception) or collected.hide:
        incomplete = True
    else:
        total += collected.total
        append(collected.playlists, False)
        page_count = (min(max(0, limit-created_count), collected.total)+99)//100
        pages = await asyncio.gather(*(bounded(client.user.get_fav_songlist(credential.encrypt_uin, num=100, page=p)) for p in range(2, page_count+1)), return_exceptions=True)
        for data in pages:
            if isinstance(data, Exception) or data.hide:
                incomplete = True
                break
            append(data.playlists, False)
            incomplete = incomplete or bool(data.failed_ids)
        incomplete = incomplete or bool(collected.failed_ids)
    return {'title': 'QQ Music · Playlists', 'items': items[:limit], 'total': total,
            'more': len(items) > limit or (not isinstance(collected, Exception) and not collected.hide and total > limit),
            'notice': 'Some QQ Music playlists could not be loaded. Refresh or sign in again to try again.' if incomplete else ''}


async def home(client):
    """Typed music recommendations: every card has a supported browse/play target."""
    specifications = [
        ('Made for you', 'songs', 'song', 12, client.recommend.get_guess_recommend()),
        ('Recommended playlists', 'songlists', 'playlist', 18, client.recommend.get_recommend_songlist(num=18)),
        ('New songs', 'songs', 'song', 18, client.recommend.get_recommend_newsong()),
        ('New albums', 'albums', 'album', 12, client.album.get_new_album(num=12)),
    ]
    responses = await asyncio.gather(*(asyncio.wait_for(request, timeout=22) for *_, request in specifications), return_exceptions=True)
    sections = []
    failed = False
    for (title, field, kind, limit, _), response in zip(specifications, responses):
        if isinstance(response, Exception):
            failed = True
            continue
        items, seen = [], set()
        for item in getattr(response, field)[:limit]:
            try:
                normalized = normalize(item, kind)
            except (ValueError, TypeError, AttributeError):
                failed = True
                continue
            if normalized['id'] in seen:
                continue
            seen.add(normalized['id'])
            items.append(normalized)
        if items:
            sections.append({'title': title, 'items': items})
    if not sections:
        raise QQMusicError('QQ Music recommendations could not be loaded. Check your connection and refresh, or sign in again if your credentials have expired.')
    return {'title': 'QQ Music', 'sections': sections,
            'notice': 'Some recommendations could not be loaded. Refresh to try again.' if failed else ''}


def buffer_audio(data, directory):
    """Bound temporary buffering, in a private temporary directory."""
    from urllib.request import Request, urlopen
    directory = Path(directory).resolve(strict=True)
    path = directory / 'qqmusic.mp3'
    maximum = 32 * 1024 * 1024
    try:
        with urlopen(Request(data['url'], headers={'User-Agent': 'Sung', 'Referer': 'https://y.qq.com/'}), timeout=18) as response:
            if int(response.headers.get('Content-Length') or 0) > maximum:
                return data
            with path.open('wb') as output:
                size = 0
                while block := response.read(65536):
                    size += len(block)
                    if size > maximum:
                        path.unlink(missing_ok=True)
                        return data
                    output.write(block)
        if not size:
            raise QQMusicError('QQ Music returned empty audio')
        return {'file': str(path)}
    except Exception:
        path.unlink(missing_ok=True)
        raise


def run(req):
    try:
        from qqmusic_api import Credential
    except ImportError:
        raise RuntimeError('QQ Music helper is unavailable. Run scripts/setup.sh to install qqmusic-api-python==0.8.2.') from None
    if req.get('op') == 'login':
        from qqmusic_login import run_app
        return run_app(req)
    credential = load_credential(req.get('credentialFile'), Credential)

    async def execute():
        async with create_client(credential=credential, device_path=req.get('devicePath')) as client:
            return await asyncio.wait_for(dispatch(req, client), timeout=35)

    try:
        data = asyncio.run(execute())
        if req.get('op') == 'buffer':
            data = buffer_audio(data, req['directory'])
        return data
    except QQMusicError:
        raise
    except TimeoutError:
        raise RuntimeError('QQ Music connection timed out. Try again.') from None
    except Exception as exc:
        # Do not return upstream exception text: requests can contain login keys.
        raise RuntimeError('QQ Music request failed (%s). Check your connection or renew your credentials.' % type(exc).__name__) from None
