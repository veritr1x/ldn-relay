#!/usr/bin/env python3
"""Summarize completed physical BLE experiments without promoting them to Switch proof."""
import json
from pathlib import Path
root=Path(__file__).resolve().parents[2]
out=root/'build/ble-central'
files=[]
for pattern in ['run-*.json','adaptive-rto-62.json','adaptive-rto-93.json','window4-93.json','hill1-w*-93.json','hill2-w*-93.json','confirm-w*-93.json','confirm62-w*-62.json']:
    files += sorted(out.glob(pattern))
lines=['# iPhone BLE hill climb', '', '**Physical Mac central ↔ iPhone 15 Pro peripheral. No Switch measurements here.**', '',
       'Each run offers 60 seconds of 130-byte echo records. RTT includes both directions; '
       'these are not independent asymmetric game traffic or one-way latency tests. '
       'All current candidates use adaptive batching, zero artificial submission delay '
       'and the ACK-based retry timer. The original baseline used fixed pacing/batching '
       'and the fixed retry timer. Small differences between runs can reflect radio variation.', '',
       '| Run | Rate/s | Sender window each way | Returned/generated | Drops | Median RTT ms | p95 RTT ms | Retries |',
       '|:---|---:|---:|---:|---:|---:|---:|---:|']
for path in files:
    r=json.loads(path.read_text())
    if r.get('reason')!='completed':continue
    lines.append(f"| [{path.stem}]({path.name}) | {r['rate']} | {r.get('central_window',8)} | {r['echoed']}/{r['generated']} | {r['app_dropped']} | {r['rtt_p50_ms']} | {r['rtt_p95_ms']} | {r['retries']} |")
lines += ['', 'Rows with drops are failures regardless of latency. Completed means the benchmark '
          'ended, not that it passed. Metadata files alongside sweep results record both '
          'binaries and source hashes. The confirmation runs bracket the candidate with '
          'an eight-frame control to check variation. The fixed 500 ms timer experiment '
          'failed local recovery tests and was discarded. An overlapping-client run was '
          'invalidated; the benchmark now enforces a process lock.', '',
          'Switch testing is deferred at the user’s request while iPhone tuning continues. '
          'The rebuilt diagnostic NRO is not evidence of console success.', '']
(out/'HILL-CLIMB.md').write_text('\n'.join(lines))
print(out/'HILL-CLIMB.md')
