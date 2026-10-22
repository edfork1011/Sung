"""QQ Music and local media helper; one JSON request per process."""


import json
import re
import sys


def sample_depth(stream):
    raw = stream.get('bits_per_raw_sample')
    try:
        if raw and 0 < int(raw) <= 64: return int(raw)
    except (TypeError, ValueError): pass
    if str(stream.get('codec_name') or '').lower() not in ('flac','alac','wavpack','pcm_s16le','pcm_s24le','pcm_s32le','tta','ape'): return 0
    depths = {'s16':16,'s16p':16,'s32':32,'s32p':32,'fltp':32,'flt':32,'u8':8,'u8p':8}
    return depths.get(str(stream.get('sample_fmt') or '').lower(), 0)


GAIN_TAGS = {'replaygain_track_gain':'replaygainTrackGain','replaygain_album_gain':'replaygainAlbumGain','r128_track_gain':'r128TrackGain'}


AUDIO_EXTENSIONS = {'.mp3','.flac','.ogg','.opus','.m4a','.aac','.wav','.aiff','.aif','.wma'}


ART_EXTENSIONS = ('.gif', '.webp', '.mp4', '.webm', '.jpg', '.jpeg', '.png')


def local_cover(path, directories):
    """Track-specific sidecars precede shared album covers; names ignore case."""
    import os
    if path.parent not in directories:
        try:
            matches = {}
            with os.scandir(path.parent) as entries:
                for entry in entries:
                    if not entry.name.lower().endswith(ART_EXTENSIONS) or not entry.is_file(): continue
                    key = entry.name.casefold()
                    if key not in matches or entry.name < matches[key].name:
                        matches[key] = path.parent / entry.name
            directories[path.parent] = matches
        except OSError:
            directories[path.parent] = {}
    names = directories[path.parent]
    for stem in (path.stem, 'cover', 'folder', 'front', 'artwork'):
        for extension in ART_EXTENSIONS:
            candidate = names.get((stem + extension).casefold())
            if candidate: return candidate
    return None


def tag_number(value):
    try: return max(1, min(9999, int(str(value or '1').split('/')[0])))
    except (ValueError, TypeError): return 1


def local_stamp(path, cover):
    st = path.stat()
    stamp = f'{st.st_mtime_ns}:{st.st_size}'
    if cover:
        try:
            st = cover.stat()
            stamp += f'|{cover.name}:{st.st_mtime_ns}:{st.st_size}'
        except OSError: pass
    return stamp


def cover_poster(cover, directory):
    """Cache a bounded poster. Failure must never reject the audio import."""
    import hashlib, os, subprocess
    from pathlib import Path
    try:
        st = cover.stat()
        if st.st_size > 128*1024*1024: return '', ''
        probe = subprocess.run(['ffprobe','-v','error','-protocol_whitelist','file,crypto,data',
                                '-select_streams','v:0','-show_entries','stream=width,height',
                                '-of','json',str(cover)], capture_output=True, timeout=5)
        streams = json.loads(probe.stdout).get('streams', []) if not probe.returncode and len(probe.stdout)<16384 else []
        if not streams or not (0 < streams[0].get('width',0) <= 4096 and 0 < streams[0].get('height',0) <= 4096): return '', ''
        directory = Path(directory); directory.mkdir(parents=True, exist_ok=True)
        identity = hashlib.sha256(os.fsencode(str(cover))).hexdigest()
        target = directory / ('cover_' + identity + '.jpg')
        stamp = f'{st.st_mtime_ns}:{st.st_size}'
        marker = target.with_suffix('.stamp')
        if not target.is_file() or not marker.is_file() or marker.read_text()!=stamp:
            if sum(f.stat().st_size for f in directory.glob('*.jpg')) >= 48*1024*1024 and not target.exists(): return '', ''
            temporary = target.with_suffix('.tmp.jpg')
            try:
                result = subprocess.run(['ffmpeg','-nostdin','-v','error','-threads','1',
                    '-protocol_whitelist','file,crypto,data','-i',str(cover),'-map','0:v:0',
                    '-frames:v','1','-vf','scale=512:512:force_original_aspect_ratio=decrease',
                    '-threads','1','-q:v','4','-y',str(temporary)],capture_output=True,timeout=5)
                if result.returncode or not temporary.is_file() or temporary.stat().st_size>262144: return '', ''
                temporary.replace(target); marker.write_text(stamp)
            finally:
                temporary.unlink(missing_ok=True)
        motion = cover.as_uri()+'?v='+stamp if cover.suffix.lower() in ART_EXTENSIONS[:4] else ''
        return target.as_uri()+'?v='+stamp, motion
    except (OSError, ValueError, subprocess.TimeoutExpired):
        return '', ''


def scan_music_folders(req):
    import os
    from pathlib import Path
    known = req.get('known', {})
    files, failed, seen, visited = [], 0, set(), set()
    count, payload, limited = 0, 0, False
    directories = {}
    def scan(directory, depth=0):
        nonlocal count, payload, limited, failed
        if limited: return
        if depth > 64:
            limited = True
            return
        try:
            canonical = str(Path(directory).resolve())
            if canonical in visited: return
            visited.add(canonical)
            with os.scandir(directory) as entries:
                for entry in entries:
                    count += 1
                    if count > 100000:
                        limited = True
                        return
                    if entry.is_dir(follow_symlinks=False):
                        scan(entry.path, depth+1)
                    elif Path(entry.name).suffix.lower() in AUDIO_EXTENSIONS and entry.is_file():
                        path = Path(entry.path).resolve()
                        if str(path) in seen: continue
                        seen.add(str(path))
                        stamp = local_stamp(path, local_cover(path, directories))
                        if known.get(str(path)) == stamp: continue
                        size = len(str(path).encode('utf-8'))
                        if len(files) >= 10000 or payload+size > 1048576:
                            limited = True
                            return
                        files.append(str(path)); payload += size
                    if limited: return
        except OSError:
            failed += 1
    for directory in req.get('folders', [])[:64]: scan(directory)
    roots = [str(Path(p).resolve()) for p in req.get('folders', [])[:64]]
    missing = [p for p in known if any(p.startswith(r+os.sep) for r in roots) and p not in seen] if not failed and not limited else []
    watches = sorted(visited)[:4096] + sorted(seen)[:10000]
    for root in roots:
        if not Path(root).is_dir():
            ancestor = Path(root).parent
            while not ancestor.exists() and ancestor != ancestor.parent:
                ancestor = ancestor.parent
            watches.append(str(ancestor))
    return {'files': files, 'failed': failed, 'limited': limited, 'missing': missing, 'watchPaths': watches}


def playlist_cleanup(req):
    from pathlib import Path
    import stat
    seen, issues = set(), []
    for index, row in enumerate(req.get('rows', [])):
        path = row.get('localPath', '')
        missing = False
        if path:
            try:
                canonical = str(Path(path).resolve())
                missing = not stat.S_ISREG(Path(path).stat().st_mode)
            except FileNotFoundError:
                canonical = path
                missing = True
            except OSError:
                canonical = path
            key = 'file:' + canonical
        else:
            key = row.get('source', 'saved') + ':' + (row.get('remoteId') or row.get('id') or str(index))
        duplicate = key in seen
        seen.add(key)
        if missing or duplicate: issues.append({'index':index, 'duplicate':duplicate, 'missing':missing})
    return {'issues':issues}


def local_files(req):
    import subprocess, hashlib, math, os
    from pathlib import Path
    allowed = AUDIO_EXTENSIONS
    items, errors = [], []
    directories = {}
    for name in req.get('files', [])[:4]:
        path = Path(name).resolve()
        try:
            if path.suffix.lower() not in allowed or not path.is_file(): raise ValueError('Missing or unsupported audio file')
            probe = subprocess.run(['ffprobe','-v','error','-protocol_whitelist','file,crypto,data','-show_entries','format=duration:format_tags=title,artist,album,album_artist,albumartist,track,disc,date,year,genre,composer,replaygain_track_gain,replaygain_album_gain,r128_track_gain:stream=codec_type,codec_name,sample_rate,bit_rate,channels,bits_per_raw_sample,sample_fmt:stream_tags=genre,composer,replaygain_track_gain,replaygain_album_gain,r128_track_gain:stream_disposition=attached_pic','-of','json',str(path)],capture_output=True,timeout=5)
            if probe.returncode or len(probe.stdout)>262144: raise ValueError('Could not read audio metadata')
            data = json.loads(probe.stdout)
            if not any(stream.get('codec_type')=='audio' for stream in data.get('streams',[])): raise ValueError('No audio stream')
            audio = next(stream for stream in data['streams'] if stream.get('codec_type') == 'audio')
            info = data.get('format',{}); tags = {k.lower():v for k,v in info.get('tags',{}).items()}
            # Vorbis comments live on the audio stream; ID3 lives on the container.
            tags = {**{k.lower():v for k,v in audio.get('tags',{}).items()}, **tags}
            # FLAC and Opus keep loudness tags on the audio stream, MP3 on the container.
            gain = {**{k:str(tags[k])[:32] for k in GAIN_TAGS if tags.get(k)},
                    **{k.lower():str(v)[:32] for k,v in audio.get('tags',{}).items() if k.lower() in GAIN_TAGS and v}}
            seconds = float(info.get('duration') or 0)
            if not math.isfinite(seconds) or seconds<0 or seconds>604800: seconds=0
            identity = 'local_' + hashlib.sha256(os.fsencode(str(path))).hexdigest()
            cover = local_cover(path, directories)
            art, motion = cover_poster(cover, req['artDirectory']) if cover and req.get('artDirectory') else ('', '')
            if not art and req.get('artDirectory') and any(v.get('disposition',{}).get('attached_pic') for v in data.get('streams',[])):
                directory=Path(req['artDirectory']);directory.mkdir(parents=True,exist_ok=True)
                target=directory/(identity+'.jpg')
                if target.exists() or sum(f.stat().st_size for f in directory.glob('*.jpg'))<48*1024*1024:
                    try:
                        extraction=subprocess.run(['ffmpeg','-nostdin','-v','error','-threads','1','-protocol_whitelist','file,crypto,data','-i',str(path),'-map','0:v:0','-frames:v','1','-vf',"scale=512:512:force_original_aspect_ratio=decrease",'-threads','1','-q:v','4','-y',str(target)],capture_output=True,timeout=5)
                        if extraction.returncode==0 and target.is_file() and target.stat().st_size<=262144: art=target.as_uri()+"?v="+str(path.stat().st_mtime_ns)
                        elif target.exists(): target.unlink()
                    except (OSError, subprocess.TimeoutExpired):
                        if target.exists(): target.unlink()
            items.append(dict(**{GAIN_TAGS[k]:v for k,v in gain.items()},id=identity,kind='song',localPath=str(path),localStamp=local_stamp(path, cover),title=str(tags.get('title') or path.stem)[:512],artist=str(tags.get('artist') or '')[:512],album=str(tags.get('album') or '')[:512],albumArtist=str(tags.get('album_artist') or tags.get('albumartist') or '')[:512],trackNumber=tag_number(tags.get('track')),discNumber=tag_number(tags.get('disc')),year=str(tags.get('date') or tags.get('year') or '')[:4],seconds=round(seconds),duration=f'{int(seconds)//60}:{int(seconds)%60:02d}' if seconds else '',art=art,motionArt=motion,codec=str(audio.get('codec_name') or '').upper(),sampleRate=int(audio.get('sample_rate') or 0),bitrate=int(audio.get('bit_rate') or 0),channels=int(audio.get('channels') or 0),bitDepth=sample_depth(audio),genre=str(tags.get('genre') or '')[:120],composer=str(tags.get('composer') or '')[:512],available=True))
        except (OSError, ValueError, subprocess.TimeoutExpired):
            errors.append(path.name)
    return {'items':items,'failed':errors}


def run(req):
    op = req.get('op', '')
    if op == 'choose-artwork':
        from pathlib import Path
        cover = Path(req.get('path', ''))
        if not cover.is_absolute() or not cover.is_file() or cover.suffix.lower() not in ART_EXTENSIONS[:4]:
            return {'motionArt': ''}
        _, motion = cover_poster(cover, req['artDirectory'])
        return {'motionArt': motion}
    if op == 'local-files': return local_files(req)
    if op == 'scan-folders': return scan_music_folders(req)
    if op == 'playlist-cleanup': return playlist_cleanup(req)
    if op == 'local-lyrics': return {'lyrics': '', 'lines': []}
    if req.get('source', 'qqmusic') != 'qqmusic':
        raise ValueError('Only QQ Music is supported for online music')
    if op not in {'home', 'search', 'song', 'album', 'playlist', 'artist', 'link', 'lyrics', 'resolve', 'buffer', 'login', 'account', 'like', 'radio'}:
        raise ValueError('Unknown request')
    from qqmusic import run as qqmusic_run
    return qqmusic_run(req)


if __name__ == '__main__':
    try:
        payload = sys.stdin.read(16*1024*1024+1)
        if len(payload)>16*1024*1024: raise ValueError('Request is too large')
        data = run(json.loads(payload))
        print(json.dumps({'ok': True, **data}, ensure_ascii=False))
    except Exception as exc:
        print(json.dumps({'ok': False, 'error': str(exc)[-1800:]}))
        sys.exit(1)
