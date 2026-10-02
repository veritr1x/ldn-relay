#!/usr/bin/env python3
"""Compare batching/pacing/window hypotheses, never change production transport."""
import json
from ble_lab import DEFAULT, ROOT, Lab
out = ROOT / 'build/ble-lab/tuning'
out.mkdir(parents=True, exist_ok=True)
rows = []
scenarios = {
    'current-model': {},
    'batch-20ms': dict(batch_switch_ms=20, batch_apple_ms=20),
    'batch-20ms-pace5': dict(batch_switch_ms=20, batch_apple_ms=20, pace_switch_ms=5, pace_apple_ms=5),
    'asymmetric-window1': dict(window_apple=1, batch_switch_ms=20, batch_apple_ms=20),
    'snapshot-window8': dict(switch_snapshot=True, poll_switch_ms=30),
    'snapshot-window1': dict(switch_snapshot=True, poll_switch_ms=30, window_apple=1,
                             batch_switch_ms=20, batch_apple_ms=20),
}
for budget in (14,20):
    for rate in (62,93):
        for name,changes in scenarios.items():
            cfg = dict(DEFAULT, duration_ms=60000, shared_fragments=budget, rate_switch=rate, rate_apple=rate)
            cfg.update(changes)
            r = Lab(cfg).run()
            (out / f'{budget}-{rate}-{name}.json').write_text(json.dumps(r,indent=2)+'\n')
            endpoints = r['endpoints']
            a,s = endpoints['apple'], endpoints['switch']
            drops = sum(x['app_dropped'] for x in endpoints.values())
            pending = sum(x['undelivered_accepted'] for x in endpoints.values())
            row = [budget,rate,name,a['latency_ms']['p95'],s['latency_ms']['p95'],drops,pending,
                   sum(x['callback_frames_lost'] for x in endpoints.values()),
                   sum(x['retries'] for x in endpoints.values())]
            rows.append(row)
            print(row)
lines = ['# Local scheduling comparison', '',
         '**Hypothetical link settings, not Switch measurements. Production code is unchanged.**', '',
         '60 s generated traffic + 10 s drain. Packet sizes 130/170 bytes, including relay headers; '
         '27-byte LL payload, 15 ms event interval. Effective shared fragment budget is assumed. '
         'Sender window caps apply to new data; retry and ACK-only frames still use the real stream code. '
         'A window of one does not suppress ACK-only notifications, so it is not a complete '
         'one-event-at-a-time credit protocol. Snapshot loss includes these frames too.', '',
         '| Budget | pps each way | Scenario | To Apple p95 ms | To Switch p95 ms | App drops | Undelivered | Callback loss | Retries |',
         '|---:|---:|:---|---:|---:|---:|---:|---:|---:|']
lines += ['| '+' | '.join(map(str,row))+' |' for row in rows]
lines += ['', 'Batch scheduling is simulated, not the native application scheduler. Holding packets '
          'for batching changes latency and does not reduce the original game-message count. '
          'These runs do not test Pia timing or a trade. Do not select a higher radio budget '
          'to make a change pass.', '']
(out/'REPORT.md').write_text('\n'.join(lines))
