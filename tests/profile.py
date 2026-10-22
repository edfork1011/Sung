#!/usr/bin/env python3
"""Measure an isolated, idle UI. Does not touch the running desktop player."""
import json,os,pathlib,subprocess,sys,time,tempfile
binary=sys.argv[1]
profile=tempfile.TemporaryDirectory(prefix='sung-profile-')
env=os.environ.copy();env.update(XDG_CONFIG_HOME=profile.name+'/config',XDG_DATA_HOME=profile.name+'/data',XDG_CACHE_HOME=profile.name+'/cache',SUNG_HELPER=str(pathlib.Path(__file__).resolve().with_name('catalog_fixture.py')),SUNG_PYTHON=sys.executable,QT_QPA_PLATFORM='offscreen',QT_QPA_PLATFORMTHEME='generic',QT_QUICK_BACKEND='software')
proc=subprocess.Popen([binary,'--isolated']+(['--mini'] if '--mini' in sys.argv else []),env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
try:
    time.sleep(2)
    if proc.poll() is not None:raise RuntimeError('UI exited during startup')
    def ticks():
        parts=pathlib.Path(f'/proc/{proc.pid}/stat').read_text().split(') ',1)[1].split()
        return int(parts[11])+int(parts[12])
    start=time.monotonic();before=ticks();time.sleep(5);cpu=(ticks()-before)/os.sysconf('SC_CLK_TCK')/(time.monotonic()-start)*100
    memory={}
    for line in pathlib.Path(f'/proc/{proc.pid}/smaps_rollup').read_text().splitlines():
        parts=line.split()
        if parts and parts[0] in ('Rss:','Pss:'):memory[parts[0][:-1].lower()+'_mib']=round(int(parts[1])/1024,2)
    print(json.dumps({'workload':('isolated idle mini player' if '--mini' in sys.argv else 'isolated idle UI')+', offline, 5-second sample','cpu_percent_one_core':round(cpu,2),**memory,'binary_bytes':pathlib.Path(binary).stat().st_size},indent=2))
finally:
    if proc.poll() is None:proc.terminate();proc.wait(timeout=5)
    profile.cleanup()
