#!/usr/bin/env python3
"""Run sequential physical iPhone window experiments; never overlap clients."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser()
p.add_argument('--profile',type=Path,required=True)
p.add_argument('--device',required=True)
p.add_argument('--windows',default='2,3')
p.add_argument('--rate',type=int,default=93)
p.add_argument('--prefix',required=True)
a=p.parse_args()
windows=[int(w) for w in a.windows.split(',')]
assert all(1<=w<=8 for w in windows)
assert a.prefix and all(c.isalnum() or c in '-_' for c in a.prefix)
out=root/'build/ble-central'
profile=a.profile.resolve()
for window in windows:
    stem=out/f'{a.prefix}-w{window}-{a.rate}'
    result=stem.with_suffix('.json')
    if result.exists():
        raise SystemExit(f'Refusing to overwrite {result}')
    commands=[
      [sys.executable,'relay/ios/build.py','--ble-pace','0','--ble-window',str(window),'--profile',str(profile)],
      ['xcrun','devicectl','device','install','app','--device',a.device,'build/relay-ios-v0.3.1/LDNRelay.app'],
      ['xcrun','devicectl','device','process','launch','--device',a.device,'dev.local.ldn-relay']]
    print(f'Preparing window={window}, rate={a.rate}',flush=True)
    with stem.with_suffix('.deployment.log').open('w') as log:
        for cmd in commands:
            subprocess.run(cmd,cwd=root,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
    files=['relay/common/relay_stream.c','relay/common/relay_stream.h','relay/common/LRTransport.m','relay/ble-lab/mac-central.m','build/relay-ios-v0.3.1/LDNRelay.app/LDNRelay','build/ble-central/BLE Central Lab.app/Contents/MacOS/BLECentral']
    meta=dict(window=window,rate=a.rate,iphone_pace=0,central_pace=0,central_batch_wait=0,
              sha256={f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in files})
    stem.with_suffix('.metadata.json').write_text(json.dumps(meta,indent=2)+'\n')
    with stem.with_suffix('.log').open('w') as log:
        subprocess.run([str(out/'BLE Central Lab.app/Contents/MacOS/BLECentral'),str(a.rate),str(result),'0','0',str(window)],cwd=root,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
    r=json.loads(result.read_text())
    print(f"window={window} returned={r['echoed']}/{r['generated']} drops={r['app_dropped']} p50={r['rtt_p50_ms']} p95={r['rtt_p95_ms']} retries={r['retries']}",flush=True)
print('Sweep completed',flush=True)
