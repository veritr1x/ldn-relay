#!/usr/bin/env python3
"""Sweep hypothetical shared radio budgets; write reproducible JSON and Markdown."""
import json
import math
from ble_lab import DEFAULT, ROOT, Lab

out = ROOT / 'build/ble-lab/sweep'
out.mkdir(parents=True, exist_ok=True)
rows = []
for budget in (8, 14, 20, 28):
    for rate in (62, 93, 300):
        cfg = dict(DEFAULT, duration_ms=60000, rate_switch=rate, rate_apple=rate,
                   shared_fragments=budget)
        result = Lab(cfg).run()
        peers = result['endpoints']
        end = next(t for t in result['timeline'] if t['ms'] == cfg['duration_ms'])
        # End-of-load backlog includes all accepted application packets not yet
        # delivered, including platform/stream queues and unacknowledged data.
        backlog = {side:end[side]['generated']-end[side]['dropped']-end[other]['received']
                   for side,other in [('switch','apple'),('apple','switch')]}
        # Conservative bounded-backlog proxy, not a statistical stability proof:
        # allow 100 ms of offered traffic plus one maximum three-record batch.
        bounded = all(n <= math.ceil(rate*.1)+3 for n in backlog.values())
        passes = bounded and all(r['app_dropped'] == 0 and r['undelivered_accepted'] == 0
                                 and r['latency_ms']['p95'] is not None
                                 and r['latency_ms']['p95'] < 100 for r in peers.values())
        result['assessment'] = dict(simulated_pass=passes, end_of_load_backlog=backlog,
                                    bounded_backlog=bounded,
                                    notification_fallback='not modelled; v2 only')
        (out / f'budget-{budget}-rate-{rate}.json').write_text(json.dumps(result,indent=2)+'\n')
        a,s = peers['apple'],peers['switch']
        row = (budget,rate,a['latency_ms']['p95'],s['latency_ms']['p95'],
               sum(r['app_dropped'] for r in peers.values()),
               sum(r['retries'] for r in peers.values()),'PASS' if passes else 'FAIL')
        rows.append(row)
        print(row)
lines = ['# Simulated BLE capacity sweep', '',
         '**These are hypothetical configurations, not measured Switch performance.**', '',
         'Actual relay_stream.c; 60 seconds offered traffic plus 10 seconds drain. '
         'Each direction runs at the listed rate. Switch records are 130 bytes; Apple '
         'records are 170 bytes, including 10-byte relay headers. Sizes are estimates. '
         '15 ms connection-event interval and 27-byte link-layer payload are assumptions. '
         'Data-fragment budget is shared across both directions.', '',
         '| Shared fragments/event | Packets/s each way | Switch → Apple p95 ms | Apple → Switch p95 ms | App drops | Retries | Model check |',
         '|---:|---:|---:|---:|---:|---:|:---|']
lines += ['| '+' | '.join(map(str,row))+' |' for row in rows]
lines += ['', 'PASS requires no application drops, all accepted packets delivered after drain, '
          'p95 below 100 ms in both directions and end-of-load backlog no greater than '
          '100 ms of offered traffic plus three packets per direction. This finite-run '
          'check does not prove stability indefinitely. Notification fallback, native '
          'driver behavior and game protocol success are not tested.', '',
          'The JSON files contain complete configuration, counters and one-second queue '
          'samples. Budgets have not been verified against the OLED radio; do not infer '
          'that a passing budget is available on hardware.', '']
(out / 'REPORT.md').write_text('\n'.join(lines))
print(out / 'REPORT.md')
