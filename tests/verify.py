"""Build and verify the QQ Music/local-media client using isolated fixtures."""
import argparse
import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--build-dir',type=Path,default=root/'build-verification')
parser.add_argument('--jobs',type=int,default=4)
parser.add_argument('--offline',action='store_true',help='All verification uses deterministic fixtures; no live account requests are made.')
args=parser.parse_args()
out=root/'verification'/'qqmusic-only';out.mkdir(parents=True,exist_ok=True)
rows=[]
env=os.environ.copy();env.update(QT_QPA_PLATFORM='offscreen',QT_QPA_PLATFORMTHEME='generic',QT_QUICK_BACKEND='software')

def stage(name,command,timeout=600,environment=env):
    print(name,flush=True);started=time.monotonic()
    with (out/(name+'.log')).open('w') as log:
        result=subprocess.run([str(v) for v in command],cwd=root,env=environment,stdout=log,stderr=subprocess.STDOUT,timeout=timeout)
    rows.append({'stage':name,'passed':result.returncode==0,'seconds':round(time.monotonic()-started,2)})
    if result.returncode:
        print((out/(name+'.log')).read_text()[-6000:],flush=True)
        raise RuntimeError(name+' failed')

try:
    stage('configure',['cmake','-S',root,'-B',args.build_dir,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=ON','-DSUNG_DIAGNOSTICS=ON'])
    stage('build',['cmake','--build',args.build_dir,'--parallel',args.jobs],900)
    python=root/'runtime/bin/python'
    stage('python',[python if python.exists() else sys.executable,'-m','unittest','discover','-s','tests','-p','test_*.py'])
    stage('native',['ctest','--test-dir',args.build_dir,'--output-on-failure'],900)
    with tempfile.TemporaryDirectory(prefix='sung-qq-ui-') as temp:
        ui=env.copy();ui.update(XDG_CONFIG_HOME=temp+'/config',XDG_DATA_HOME=temp+'/data',XDG_CACHE_HOME=temp+'/cache',SUNG_HELPER=str(root/'tests/catalog_fixture.py'),SUNG_PYTHON=sys.executable,SUNG_BUFFER_FIXTURE='1',SUNG_QQ_ACCOUNT_STATE=temp+'/account.json',SUNG_QQ_HOME_FIXTURE='recommendations',SUNG_TEST_OUTPUT=str(out/'screenshots'))
        stage('ui',['dbus-run-session','--',args.build_dir/'sung','--isolated','--qqmusic-ui-test'],90,ui)
    passed=True
except (RuntimeError,subprocess.TimeoutExpired):
    passed=False
(out/'summary.json').write_text(json.dumps({'created':datetime.datetime.now(datetime.timezone.utc).isoformat(),'passed':passed,'stages':rows},indent=2))
print('PASS' if passed else 'FAIL',out,flush=True)
sys.exit(0 if passed else 1)
