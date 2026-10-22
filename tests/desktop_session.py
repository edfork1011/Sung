"""Verify Caelestia MPRIS, tray activation and background playback on Hyprland."""
import argparse,json,os,pathlib,shlex,signal,subprocess,tempfile,time
root=pathlib.Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary',type=pathlib.Path,default=root/'build-qq-ui/sung')
parser.add_argument('--workspace',type=int,default=95)
args=parser.parse_args();binary=args.binary.resolve()
out=root/'verification/desktop';out.mkdir(parents=True,exist_ok=True)
instance='desktop_test_'+str(os.getpid());service='org.mpris.MediaPlayer2.sung.'+instance

def call(*args,timeout=6):return subprocess.check_output(args,text=True,timeout=timeout).strip()
def shell(prop):return call('qs','-c','caelestia','ipc','call','mpris','getActive',prop)
with tempfile.TemporaryDirectory(prefix='sung-desktop-') as tmp:
 folder=pathlib.Path(tmp);settings=folder/'config/Sung/sung.conf';settings.parent.mkdir(parents=True);settings.write_text('[General]\nonboarded=true\nmotion=false\nwatchMusicFolders=false\ntrackNotifications=false\nvolume=0\n')
 for name in ('playing.ready','observed.ready','hidden.ready','restored.ready'):(out/name).unlink(missing_ok=True)
 env=dict(XDG_CONFIG_HOME=tmp+'/config',XDG_DATA_HOME=tmp+'/data',XDG_CACHE_HOME=tmp+'/cache',SUNG_HELPER=str(root/'tests/catalog_fixture.py'),SUNG_PYTHON='/usr/bin/python3',SUNG_QQ_HOME_FIXTURE='recommendations',SUNG_BUFFER_FIXTURE='1',SUNG_TEST_OUTPUT=str(out),QT_QPA_PLATFORM='wayland',QT_QPA_PLATFORMTHEME='generic',QT_QUICK_BACKEND='')
 command=[str(binary),'--instance',instance,'--desktop-ui-test']
 launcher=folder/'launch.py';launcher.write_text('import os\nos.environ.update('+repr(env)+')\nopen('+repr(tmp+'/pid')+',"w").write(str(os.getpid()))\nf=os.open('+repr(str(out/'native.log'))+',os.O_WRONLY|os.O_CREAT|os.O_TRUNC,0o600)\nos.dup2(f,1);os.dup2(f,2)\nos.execv('+repr(command[0])+','+repr(command)+')\n')
 active=json.loads(call('hyprctl','activeworkspace','-j'))['id'];workspaces=json.loads(call('hyprctl','workspaces','-j'))
 if active==args.workspace or any(w['id']==args.workspace and w['windows'] for w in workspaces):raise RuntimeError('Background workspace occupied')
 lua='hl.exec_cmd('+json.dumps(shlex.join(['python3',str(launcher)]))+', {workspace='+json.dumps(str(args.workspace)+' silent')+',float=true,size="1180 800",no_initial_focus=true,suppress_event="activate activatefocus"})'
 call('hyprctl','eval',lua)
 pid=None
 try:
  deadline=time.monotonic()+25
  while not (out/'playing.ready').exists() and time.monotonic()<deadline:
   if (folder/'pid').exists():pid=int((folder/'pid').read_text())
   time.sleep(.1)
  if not (out/'playing.ready').exists():raise RuntimeError('Playback probe not ready: '+(out/'native.log').read_text()[-3000:])
  observed={}
  for _ in range(30):
   observed={p:shell(p) for p in ('trackTitle','trackArtist','trackArtUrl','isPlaying')}
   if observed['trackTitle']=='QQ Track 0' and observed['trackArtUrl']:break
   time.sleep(.1)
  assert observed['trackTitle']=='QQ Track 0',observed
  assert observed['trackArtist']=='QQ artist',observed
  assert observed['trackArtUrl'].endswith('/desktop-cover.png'),observed
  assert observed['isPlaying']=='true',observed
  observed['dbus_playback']=call('qdbus6',service,'/org/mpris/MediaPlayer2','org.freedesktop.DBus.Properties.Get','org.mpris.MediaPlayer2.Player','PlaybackStatus')
  owner=call('qdbus6','org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus.GetNameOwner',service)
  tray=call('qdbus6','org.kde.StatusNotifierWatcher','/StatusNotifierWatcher','org.freedesktop.DBus.Properties.Get','org.kde.StatusNotifierWatcher','RegisteredStatusNotifierItems')
  tray_item=None
  for item in tray.splitlines():
   tray_service=item.split('/',1)[0]
   if int(call('qdbus6','org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus.GetConnectionUnixProcessID',tray_service))==pid:tray_item=item;break
  assert tray_item is not None,tray
  tray_service,tray_path=tray_item.split('/',1);tray_path='/'+tray_path
  observed['tray_registered']=True
  clients=json.loads(call('hyprctl','clients','-j'));client=next(c for c in clients if c['pid']==pid)
  subprocess.run(['grim','-T',client['stableId'],str(out/'native-playing.png')],check=True,timeout=10)
  (out/'observed.ready').touch()
  deadline=time.monotonic()+12
  while not (out/'hidden.ready').exists() and time.monotonic()<deadline:time.sleep(.1)
  assert (out/'hidden.ready').exists(),'Window did not reach background state'
  def status():return call('qdbus6',service,'/org/mpris/MediaPlayer2','org.freedesktop.DBus.Properties.Get','org.mpris.MediaPlayer2.Player','PlaybackStatus')
  assert status()=='Playing','Playback stopped when window closed'
  call('qdbus6',service,'/org/mpris/MediaPlayer2','org.mpris.MediaPlayer2.Player.Pause')
  assert status()=='Paused','Background MPRIS Pause failed'
  call('qdbus6',service,'/org/mpris/MediaPlayer2','org.mpris.MediaPlayer2.Player.Play')
  assert status()=='Playing','Background MPRIS Play failed'
  observed['background_controls']=True
  call('qdbus6',tray_service,tray_path,'org.kde.StatusNotifierItem.Activate','0','0')
  (out/'restored.ready').touch()
  deadline=time.monotonic()+22
  while time.monotonic()<deadline:
   log=(out/'native.log').read_text()
   if 'RESULT ' in log:break
   time.sleep(.1)
  assert 'RESULT 0 failures' in log,log[-4000:]
  observed['focus_preserved']=json.loads(call('hyprctl','activeworkspace','-j'))['id']==active
  observed['service']=service
  (out/'native-result.json').write_text(json.dumps(observed,indent=2)+'\n');print(json.dumps(observed))
  print(log[-3000:])
  call('qdbus6',service,'/org/mpris/MediaPlayer2','org.mpris.MediaPlayer2.Quit')
  time.sleep(.5)
  assert not pathlib.Path(f'/proc/{pid}/exe').exists(),'MPRIS Quit did not exit'
 finally:
  if pid:
   try:
    if pathlib.Path(f'/proc/{pid}/exe').resolve()==binary:os.kill(pid,signal.SIGTERM)
   except FileNotFoundError:pass
