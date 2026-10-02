#!/usr/bin/env python3
"""One isolated physical iPhone experiment; refuses overwrites."""
import argparse,os,json,subprocess,sys,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--name',required=True);p.add_argument('--window',type=int,default=6);p.add_argument('--ack',type=int,default=0);p.add_argument('--rate',type=int,default=62);p.add_argument('--compact',action='store_true');p.add_argument('--duplex',action='store_true');p.add_argument('--duplex-out-size',type=int,default=0);p.add_argument('--device',required=True);p.add_argument('--profile',type=Path,required=True);p.add_argument('--app',type=Path,default=Path('build/relay-ios-v0.3.15-rx-credit-f500-w3/LDNRelay.app'));a=p.parse_args();a.profile=a.profile.resolve();a.app=a.app.resolve()
root=Path(__file__).resolve().parents[2];os.chdir(root)
assert all(c.isalnum() or c in '-_' for c in a.name)
stem=root/'build/ble-central'/a.name
assert not stem.with_suffix('.json').exists()
device=a.device
cmd=[sys.executable,'relay/ios/build.py','--profile',str(a.profile),'--ble-pace','0','--ble-window',str(a.window),'--ack-delay',str(a.ack)]
if a.compact:cmd+=['--compact-loopback']
if a.duplex:cmd+=['--duplex']
with stem.with_suffix('.deployment.log').open('w') as f:
 for c in [cmd,['xcrun','devicectl','device','install','app','--device',device,str(a.app)],['xcrun','devicectl','device','process','launch','--device',device,'dev.local.ldn-relay']]:subprocess.run(c,check=True,stdout=f,stderr=subprocess.STDOUT,timeout=90)
files=['relay/common/LRDuplex.inc','relay/common/relay_game_load.h','relay/common/relay_stream.c','relay/common/LRTransport.m','relay/common/relay_compact.h','relay/ble-lab/mac-central.m',str(a.app/'LDNRelay'),'build/ble-central/BLE Central Lab.app/Contents/MacOS/BLECentral']
stem.with_suffix('.metadata.json').write_text(json.dumps(dict(parameters={k:str(v) if isinstance(v,Path) else v for k,v in vars(a).items() if k not in ('device','profile')},sha256={x:hashlib.sha256(Path(x).read_bytes()).hexdigest() for x in files}),indent=2))
env=os.environ.copy();env.pop('LDN_DUPLEX_TEST',None);env.pop('LDN_COMPACT_TEST',None);env['LDN_ACK_DELAY_MS']=str(a.ack);env['LDN_DUPLEX_OUT_BYTES']=str(a.duplex_out_size)
if a.compact:env['LDN_COMPACT_TEST']='1'
if a.duplex:env['LDN_DUPLEX_TEST']='1'
with stem.with_suffix('.log').open('w') as f:
 subprocess.run(['build/ble-central/BLE Central Lab.app/Contents/MacOS/BLECentral',str(a.rate),str(stem.with_suffix('.json')),'0','0',str(a.window)],env=env,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=180)
print(stem.with_suffix('.json').read_text(),flush=True)
subprocess.run(['xcrun','devicectl','device','copy','from','--device',device,'--domain-type','appDataContainer','--domain-identifier','dev.local.ldn-relay','--source','Documents/relay.log','--destination',str(stem.with_suffix('.device.log'))],check=True,timeout=45)
