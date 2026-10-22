"""Deterministic transport fixture. Never used by the shipped application."""
import json, os, sys, time
r = json.load(sys.stdin)
op=r.get('op')
# A stage that needs to prove what the application asked for, rather than what
# it did with the answer, reads this back.
if os.environ.get('SUNG_REQUEST_LOG'):
    with open(os.environ['SUNG_REQUEST_LOG'],'a') as log: log.write(json.dumps(r)+'\n')
def song(i):
    import hashlib
    mid=f'qqmid{i:09d}'
    return dict(id='qq_'+hashlib.sha256(('song:'+mid).encode()).hexdigest(),remoteId=mid,source='qqmusic',qqMusicSong=True,qqMusicId=10000+i,title=f'QQ Track {i}',kind='song',artist='QQ artist',seconds=120,available=True,mediaMid=mid,songType=1)
data={'ok':True}
if op=='login':
    from pathlib import Path
    import struct,zlib
    directory=Path(r['directory'])
    mode=os.environ.get('SUNG_QQ_LOGIN_FIXTURE','success')
    image=directory/'qr.png'
    # A valid, deterministic QR-like image for layout and decoder tests.
    # It has no account/login payload and is never used by the real helper.
    side=185
    def pixel(x,y):
        x=x//5-4;y=y//5-4
        if not (0<=x<29 and 0<=y<29):return 255
        for left,top in ((0,0),(22,0),(0,22)):
            a,b=x-left,y-top
            if 0<=a<7 and 0<=b<7:return 0 if a in (0,6) or b in (0,6) or (2<=a<=4 and 2<=b<=4) else 255
        return 0 if (x*7+y*13)%3==0 else 255
    raw=b''.join(b'\0'+bytes(pixel(x,y) for x in range(side)) for y in range(side))
    def chunk(kind,payload):return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload)&0xffffffff)
    image.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',side,side,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
    def event(name, **fields):
        packet=json.dumps({'event':name,**fields})+'\n'
        # Exercise Qt's buffering across partial reads, not just full lines.
        middle=len(packet)//2
        sys.stdout.write(packet[:middle]);sys.stdout.flush();time.sleep(.01)
        sys.stdout.write(packet[middle:]);sys.stdout.flush()
    event('qr',path=str(Path(__file__).resolve()) if mode=='invalid-image' else str(image))
    event('waiting')
    if mode=='waiting':time.sleep(10)
    elif mode=='expired':event('expired');data={'ok':False,'error':'The QR code expired. Generate a new code to try again.'}
    elif mode=='refused':event('refused');data={'ok':False,'error':'Login was declined on your phone.'}
    else:
        time.sleep(.1)
        event('scanned')
        credential=directory/'credential.json'
        credential.write_text(json.dumps({'musicid':222 if r.get('loginType')=='wx' else 111,'musickey':'' if mode=='invalid-credential' else 'fixture-only-key'}))
        data['credentialFile']=str(credential)
elif op=='home':
    mode=os.environ.get('SUNG_QQ_HOME_FIXTURE')
    if mode=='error':data={'ok':False,'error':'QQ Music recommendations could not be loaded.'}
    elif mode:
        def collection(kind,mid,title):
            import hashlib
            return dict(id='qq_'+hashlib.sha256((kind+':'+mid).encode()).hexdigest(),remoteId=mid,browseId=mid,source='qqmusic',kind=kind,title=title,artist='QQ artist',available=True)
        data.update(title='QQ Music',sections=[
            {'title':'Made for you','items':[song(i) for i in range(1,6)]},
            {'title':'Recommended playlists','items':[collection('playlist','12345','Recommended playlist')]},
            {'title':'New songs','items':[song(10)]},
            {'title':'New albums','items':[collection('album','newAlbum','New album')]},
        ],notice='Some recommendations could not be loaded. Refresh to try again.' if mode=='partial' else '')
    else:data['sections']=[{'title':'Fixture shelf','items':[song(1)]}]
elif op in ('account','like'):
    from pathlib import Path
    import hashlib
    state_path=Path(os.environ['SUNG_QQ_ACCOUNT_STATE']) if os.environ.get('SUNG_QQ_ACCOUNT_STATE') else None
    state=json.loads(state_path.read_text()) if state_path and state_path.exists() else {}
    identity=str(json.loads(Path(r['credentialFile']).read_text())['musicid'])
    likes=state.get(identity,[1,2] if identity!='222' else [7])
    mode=os.environ.get('SUNG_QQ_ACCOUNT_FIXTURE','')
    if mode=='slow':time.sleep(1)
    if mode=='error' or (op=='like' and mode=='like-error'):data={'ok':False,'error':'QQ Music account request failed. Sign in again.'}
    elif op=='like':
        index=int(r['id'][-9:])
        if r['liked'] and index not in likes:likes.insert(0,index)
        elif not r['liked'] and index in likes:likes.remove(index)
        state[identity]=likes
        if state_path:state_path.write_text(json.dumps(state))
        data['liked']=r['liked']
    elif r.get('mode')=='favorites':
        if mode=='paged':likes=list(range(150))
        data.update(title='QQ Music · Liked songs',items=[song(i) for i in likes[:r.get('limit',100)]],total=len(likes),more=len(likes)>r.get('limit',100))
    else:
        def playlist(mid,title,owned):return dict(id='qq_'+hashlib.sha256(('playlist:'+mid).encode()).hexdigest(),remoteId=mid,browseId=mid,source='qqmusic',kind='playlist',title=title,artist='QQ owner',count=2,owned=owned,playlistOrigin='Created' if owned else 'Collected')
        data.update(title='QQ Music · Playlists',items=[playlist('12345','My QQ playlist',True),playlist('23456','Collected QQ playlist',False)],more=False,notice='Some playlists could not be loaded.' if mode=='partial' else '')
elif op=='search':
    if r['query']=='slow': time.sleep(2)
    if r['query']=='error': data={'ok':False,'error':'ConnectionError: fixture offline'}
    else: data.update(items=[song(i) for i in range(r.get('limit',30))] if r['query']!='empty' else [],more=r.get('limit',30)<200 and r['query']!='empty')
elif op in ('album','playlist','link'):
    data.update(title='Collection',items=[song(1),song(2)],total=2)
    import os
    if op=='album' and os.environ.get('SUNG_ALBUM_FIXTURE'):
        data.update(artist='Test artist', year='2026', art=os.environ['SUNG_ALBUM_FIXTURE'])
        data['items'][0]['discNumber']=1;data['items'][1]['discNumber']=2
elif op=='artist': data.update(title='Test artist',sections=[{'title':'Songs','items':[song(1)]}])
elif op=='lyrics':
    data['lyrics']='Test lyrics'
    if r.get('id')=='timedlyric1':data['lines']=[{'start':1000,'end':2000,'text':'First'},{'start':3000,'end':4000,'text':'Second'}]
elif op in ('local-files','scan-folders','playlist-cleanup','choose-artwork'):
    import importlib.util
    from pathlib import Path
    spec=importlib.util.spec_from_file_location('catalog',Path(__file__).parents[1]/'helper/catalog.py');catalog=importlib.util.module_from_spec(spec);spec.loader.exec_module(catalog)
    data.update(catalog.run(r))
elif op=='local-lyrics':data.update(lyrics='',lines=[])
elif op=='radio': data['items']=[song(i) for i in range(1,5)]
elif op in ('resolve','buffer'):
    import os, wave
    from pathlib import Path
    if os.environ.get('SUNG_BUFFER_FIXTURE') == '1':
        directory=Path(r['directory'])
        if r['id'].endswith('3'): time.sleep(.4)
        if r['id'].endswith('4'): data={'ok':False,'error':'Expected preparation failure'}
        else:
            path=directory/(r['id']+'.wav')
            with wave.open(str(path),'wb') as audio:
                audio.setnchannels(1);audio.setsampwidth(2);audio.setframerate(8000);audio.writeframes(b'\0'*2*8000*60)
            data.update(file=str(path),seconds=60)
    else: data={'ok':False,'error':'Fixture does not stream audio'}
else: data={'ok':False,'error':'Unknown fixture request'}
print(json.dumps(data))
