"""Extract payload-size samples; timestamps are too coarse for exact replay."""
from pathlib import Path
import re,json,hashlib
root=Path(__file__).resolve().parents[2]
a=root/'build/hardware-run-06/usb-0.3.0-relay.log';b=root/'build/hardware-run-06/game-0.2.3-game.log'
r={'host_to_apple':[int(x) for x in re.findall(r'UDP received: slot=0 bytes=(\d+)',a.read_text())], 'apple_to_host':[int(x) for x in re.findall(r'PIA.*tx.*len=(\d+).*rc=0',b.read_text())], 'limitations':'Host sizes sampled about every 100 packets from successful USB trade; outgoing sizes from earlier failed BLE session with old adapter. Uniform 62/s pacing is assumed in each direction. Not an exact replay or retransmission-free distribution.', 'sources':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [a,b]}}
(root/'build/ble-central/game-load.json').write_text(json.dumps(r,indent=2)+'\n')
s='/* Generated size-only synthetic load; see game-load.json for provenance. */\n#ifndef LR_GAME_LOAD_H\n#define LR_GAME_LOAD_H\n'
for name,key in [('host_sizes','host_to_apple'),('apple_sizes','apple_to_host')]:s+='static const unsigned '+name+'[]={'+','.join(map(str,r[key]))+'};\n'
s+='static inline unsigned game_size(unsigned direction,unsigned seq){return 10+(direction?apple_sizes[seq%(sizeof(apple_sizes)/sizeof(*apple_sizes))]:host_sizes[seq%(sizeof(host_sizes)/sizeof(*host_sizes))]);}\n#endif\n'
(root/'relay/common/relay_game_load.h').write_text(s)
