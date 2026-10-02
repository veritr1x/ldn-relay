#!/usr/bin/env python3
"""Model invariants and real stream regression scenarios, no hardware required."""
import json
from ble_lab import DEFAULT, PROFILES, ROOT, Lab, validate


run_number = 0


def run(profile, **overrides):
    global run_number
    run_number += 1
    cfg = dict(DEFAULT, **PROFILES[profile])
    cfg.update(overrides)
    validate(cfg)
    result = Lab(cfg).run()
    path = ROOT / 'build/ble-lab/tests' / (str(run_number)+'-'+profile+'.json')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(result, indent=2)+'\n')
    peers = result['endpoints']
    print(profile, ', '.join(f"{name}: rx={r['received']} drop={r['app_dropped']} "
          f"p95={r['latency_ms']['p95']}ms retries={r['retries']} "
          f"event_loss={r['callback_frames_lost']} pending={r['undelivered_accepted']}"
          for name,r in peers.items()))
    assert all(r['malformed'] == 0 for r in peers.values())
    return result


def clean(result):
    return all(r['app_dropped'] == 0 and r['undelivered_accepted'] == 0
               for r in result['endpoints'].values())


control = run('control')
assert clean(control)
assert all(r['latency_ms']['p95'] < 100 for r in control['endpoints'].values())
assert control == Lab(control['config']).run(), 'simulation must be deterministic'
# MTU fragmentation and complete reassembly across multiple stream frames.
fragmented = run('control', frame_bytes=64, rate_switch=5, rate_apple=5)
assert clean(fragmented)
# Unidirectional load still needs reverse acknowledgements.
oneway = run('control', rate_apple=0)
assert clean(oneway) and oneway['endpoints']['switch']['received'] == 0
baseline = run('ble41')
slow = run('slow-callback')
snapshot = run('snapshot')
assert snapshot['endpoints']['switch']['callback_frames_lost'] > 0
assert sum(r['retries'] for r in snapshot['endpoints'].values()) > 0
assert slow['endpoints']['switch']['latency_ms']['p95'] > control['endpoints']['switch']['latency_ms']['p95']
overload = run('overload')
assert not clean(overload), 'bounded queues must expose overload'
loss = run('loss', rate_switch=10, rate_apple=10)
assert clean(loss)
assert sum(r['retries'] for r in loss['endpoints'].values()) > 0
stall = run('stall', rate_switch=10, rate_apple=10)
assert clean(stall)
# A smaller shared radio budget must not magically supply the same throughput.
constrained = run('ble41', shared_fragments=4)
assert not clean(constrained)
print('PASS: deterministic delivery, fragmentation, asymmetric traffic, loss recovery, stalls, snapshot loss and capacity pressure')
# Sender caps must retain reverse ACKs and deliver low-rate asymmetric traffic.
asymmetric = run('control', window_apple=1, rate_switch=10, rate_apple=10)
assert clean(asymmetric)
# Batching can reduce radio/stream overhead; verify the motivating fixed scenario.
batched = run('ble41', batch_switch_ms=20, batch_apple_ms=20)
assert clean(batched)
assert all(r['latency_ms']['p95'] < 100 for r in batched['endpoints'].values())
