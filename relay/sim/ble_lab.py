#!/usr/bin/env python3
"""Deterministic discrete-time BLE model; executes the real relay_stream.c.

Virtual milliseconds, no hardware access. Profiles are hypotheses, not a Switch
emulator or calibrated prediction. See README.md for model boundaries.
"""
import argparse
import collections
import ctypes as C
import json
import math
import pathlib
import struct

ROOT = pathlib.Path(__file__).resolve().parents[2]
CALLBACK = C.CFUNCTYPE(C.c_bool, C.c_void_p, C.POINTER(C.c_uint8), C.c_size_t)
STAT_NAMES = ('queued_messages', 'in_flight', 'retries', 'gaps', 'duplicate_frames',
              'malformed', 'tx_frames', 'rx_frames')
DEFAULT = dict(duration_ms=10000, drain_ms=10000, rate_switch=62, rate_apple=62,
               packet_switch_bytes=130, packet_apple_bytes=170, frame_bytes=500, interval_ms=15,
               ll_payload=27, shared_fragments=14, platform_capacity=8,
               pace_switch_ms=15, pace_apple_ms=15, poll_switch_ms=1,
               poll_apple_ms=1, rx_capacity=32, switch_snapshot=False,
               batch_ms=10, batch_switch_ms=10, batch_apple_ms=10, window_switch=8, window_apple=8, batch_bytes=476, drop_every=0,
               stall_start_ms=3000, stall_ms=0)
PROFILES = {
    'control': dict(shared_fragments=1000, ll_payload=251),
    'ble41': {},
    'slow-callback': dict(poll_switch_ms=100),
    'snapshot': dict(poll_switch_ms=100, switch_snapshot=True),
    'overload': dict(rate_switch=300, rate_apple=300),
    'loss': dict(drop_every=31),
    'stall': dict(stall_ms=500),
}


def library():
    lib = C.CDLL(str(ROOT / 'build/librelay-lab.dylib'))
    signatures = {
        'lab_create': ([C.c_uint], C.c_void_p),
        'lab_destroy': ([C.c_void_p], None),
        'lab_enqueue': ([C.c_void_p, C.c_void_p, C.c_size_t], C.c_int),
        'lab_prepare': ([C.c_void_p, C.c_void_p, C.c_uint64, C.c_uint], C.c_size_t),
        'lab_commit': ([C.c_void_p, C.c_void_p, C.c_size_t, C.c_uint64], None),
        'lab_ingest': ([C.c_void_p, C.c_void_p, C.c_size_t, CALLBACK, C.c_void_p, C.c_uint64], C.c_int),
        'lab_stat': ([C.c_void_p, C.c_uint], C.c_uint64),
    }
    for name, (args, result) in signatures.items():
        fn = getattr(lib, name)
        fn.argtypes, fn.restype = args, result
    return lib


def percentile(values, p):
    return sorted(values)[max(0, math.ceil(len(values) * p) - 1)] if values else None


class Peer:
    def __init__(self, lab, side):
        self.lab, self.side = lab, side
        self.native = lab.lib.lab_create(lab.cfg['frame_bytes'])
        if not self.native:
            raise MemoryError('LrStream allocation')
        self.batch, self.batch_since = [], 0
        self.tx, self.rx = collections.deque(), collections.deque()
        self.generated = self.accepted = self.dropped = self.delivered = 0
        self.platform_blocked = self.event_loss = self.injected_loss = 0
        self.peak_tx = self.peak_rx = self.peak_stream = 0
        self.latencies, self.seen = [], set()
        self.next_submit = 0
        self.errors = []
        self.callback = CALLBACK(self.receive)

    def receive(self, _context, pointer, size):
        # Never raise through ctypes: save the failure, fail the run afterwards.
        try:
            data = C.string_at(pointer, size)
            if data[:3] != b'\x8a\0\0':
                raise ValueError('invalid synthetic batch')
            at = 3
            records = []
            while at < len(data):
                length, = struct.unpack_from('<H', data, at)
                at += 2
                packet = data[at:at+length]
                at += length
                if length != self.lab.cfg['packet_'+('apple' if self.side == 'switch' else 'switch')+'_bytes'] or len(packet) != length:
                    raise ValueError('invalid packet length')
                sequence, born = struct.unpack_from('<IQ', packet, 10)
                if packet[22:] != bytes([sequence % 251]) * (length-22):
                    raise ValueError('payload corrupted')
                if sequence in self.seen:
                    raise ValueError('duplicate application delivery')
                records.append((sequence, born))
            if at != len(data):
                raise ValueError('invalid batch length')
            for sequence, born in records:
                self.seen.add(sequence)
                self.delivered += 1
                self.latencies.append(self.lab.now-born)
            return True
        except Exception as exc:
            self.errors.append(str(exc))
            return False

    def flush(self):
        if not self.batch:
            return
        data = b'\x8a\0\0' + b''.join(struct.pack('<H', len(p))+p for p in self.batch)
        if self.lab.lib.lab_enqueue(self.native, data, len(data)):
            self.accepted += len(self.batch)
        else:
            self.dropped += len(self.batch)
        self.batch.clear()

    def generate(self):
        cfg, now = self.lab.cfg, self.lab.now
        rate = cfg['rate_'+self.side]
        target = (min(now, cfg['duration_ms'])*rate)//1000
        while self.generated < target:
            sequence = self.generated
            size = cfg['packet_'+self.side+'_bytes']
            packet = bytes([0x86 if self.side == 'switch' else 5])+bytes(9) + struct.pack('<IQ', sequence, now) + bytes([sequence % 251])*(size-22)
            if 3 + sum(2+len(p) for p in self.batch) + 2+len(packet) > cfg['batch_bytes']:
                self.flush()
            if not self.batch:
                self.batch_since = now
            self.batch.append(packet)
            self.generated += 1
        if self.batch and (now-self.batch_since >= (cfg['batch_'+self.side+'_ms'] if cfg['batch_'+self.side+'_ms'] != 10 else cfg['batch_ms']) or now >= cfg['duration_ms']):
            self.flush()

    def poll(self):
        cfg, now = self.lab.cfg, self.lab.now
        if now % cfg['poll_'+self.side+'_ms']:
            return
        if self.side == 'switch' and cfg['stall_start_ms'] <= now < cfg['stall_start_ms']+cfg['stall_ms']:
            return
        # FIFO mode drains callbacks; snapshot mode retains only the most recent
        # value since the previous poll, explicitly modelling a suspected hazard.
        while self.rx:
            data = self.rx.popleft()
            if not self.lab.lib.lab_ingest(self.native, data, len(data), self.callback, None, now):
                self.errors.append('native ingest rejected frame')

    def submit(self):
        now, cfg = self.lab.now, self.lab.cfg
        if now < self.next_submit:
            return
        buf = C.create_string_buffer(500)
        size = self.lab.lib.lab_prepare(self.native, buf, now, cfg['window_'+self.side])
        if not size:
            return
        if len(self.tx) >= cfg['platform_capacity']:
            self.platform_blocked += 1
            return  # platform rejection must not commit a prepared frame
        data = buf.raw[:size]
        # ATT opcode + handle = 3 bytes; L2CAP header = 4 bytes, once per value.
        fragments = math.ceil((size+7)/cfg['ll_payload'])
        self.tx.append([data, fragments])
        self.lab.lib.lab_commit(self.native, data, size, now)
        self.next_submit = now + cfg['pace_'+self.side+'_ms']
        self.peak_tx = max(self.peak_tx, len(self.tx))

    def arrival(self, data):
        cfg = self.lab.cfg
        if self.side == 'switch' and cfg['switch_snapshot'] and self.rx:
            self.event_loss += len(self.rx)
            self.rx.clear()
        if len(self.rx) >= cfg['rx_capacity']:
            self.event_loss += 1
        else:
            self.rx.append(data)
        self.peak_rx = max(self.peak_rx, len(self.rx))

    def result(self):
        return dict(generated=self.generated, accepted=self.accepted, app_dropped=self.dropped,
                    received=self.delivered, latency_ms=dict(p50=percentile(self.latencies,.5),
                    p95=percentile(self.latencies,.95), p99=percentile(self.latencies,.99),
                    max=max(self.latencies, default=None)),
                    platform_blocked_ticks=self.platform_blocked,
                    callback_frames_lost=self.event_loss, injected_frames_lost=self.injected_loss,
                    peak_platform_queue=self.peak_tx, peak_callback_queue=self.peak_rx,
                    peak_stream_queue=self.peak_stream,
                    platform_pending=len(self.tx), callback_pending=len(self.rx),
                    **{name:self.lab.lib.lab_stat(self.native,i) for i,name in enumerate(STAT_NAMES)})


class Lab:
    def __init__(self, cfg):
        self.cfg, self.lib, self.now = cfg, library(), 0
        self.peers = [Peer(self,'switch'), Peer(self,'apple')]
        self.turn = self.completed = self.fragments = 0
        self.timeline = []

    def radio(self):
        # Shared DATA-fragment budget across both directions, not independent
        # unidirectional throughput figures. Empty ACKs/timing are abstracted by
        # this user-supplied effective budget, NOT claimed as a PHY maximum.
        for _ in range(self.cfg['shared_fragments']):
            side = self.turn
            if not self.peers[side].tx:
                side = 1-side
            sender, receiver = self.peers[side], self.peers[1-side]
            if not sender.tx:
                break
            self.turn = 1-side
            sender.tx[0][1] -= 1
            self.fragments += 1
            if not sender.tx[0][1]:
                data, _ = sender.tx.popleft()
                self.completed += 1
                if self.cfg['drop_every'] and self.completed % self.cfg['drop_every'] == 0:
                    sender.injected_loss += 1
                else:
                    receiver.arrival(data)

    def run(self):
        try:
            for self.now in range(self.cfg['duration_ms']+self.cfg['drain_ms']+1):
                for peer in self.peers:
                    peer.generate()
                    peer.poll()
                    peer.submit()
                    peer.peak_stream = max(peer.peak_stream, self.lib.lab_stat(peer.native,0))
                if self.now and self.now % self.cfg['interval_ms'] == 0:
                    self.radio()
                if self.now % 1000 == 0:
                    self.timeline.append(dict(ms=self.now, **{p.side:dict(
                        queued=self.lib.lab_stat(p.native,0), flying=self.lib.lab_stat(p.native,1),
                        generated=p.generated, received=p.delivered, dropped=p.dropped) for p in self.peers}))
            results = {p.side:p.result() for p in self.peers}
            errors = [e for p in self.peers for e in p.errors]
            if errors:
                raise RuntimeError('; '.join(errors[:5]))
            for side, other in [('switch','apple'),('apple','switch')]:
                r = results[side]
                r['undelivered_accepted'] = r['accepted']-results[other]['received']
                assert r['generated'] == r['accepted']+r['app_dropped']
                assert r['undelivered_accepted'] >= 0
            return dict(model='assumptions, not hardware validation', config=self.cfg,
                        elapsed_virtual_ms=self.now, ll_data_fragments=self.fragments,
                        endpoints=results, timeline=self.timeline)
        finally:
            for peer in self.peers:
                self.lib.lab_destroy(peer.native)


def validate(cfg):
    for key,value in cfg.items():
        if not isinstance(value, (int,bool)) or value < 0:
            raise ValueError(f'{key} must be a nonnegative integer')
    for key in ('duration_ms','interval_ms','ll_payload','shared_fragments','platform_capacity',
                'poll_switch_ms','poll_apple_ms','pace_switch_ms','pace_apple_ms','rx_capacity'):
        if not cfg[key]:
            raise ValueError(f'{key} must be positive')
    if not 64 <= cfg['frame_bytes'] <= 500:
        raise ValueError('frame_bytes must be 64..500')
    for side in ('switch','apple'):
        if not 1 <= cfg['window_'+side] <= 8:
            raise ValueError('sender windows must be 1..8')
        if not 22 <= cfg['packet_'+side+'_bytes'] <= cfg['batch_bytes']-5 or cfg['batch_bytes'] > 2048:
            raise ValueError('packet sizes must be >=22 and fit in a batch <=2048 bytes')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=PROFILES, default='ble41')
    parser.add_argument('--set', action='append', default=[], metavar='KEY=INTEGER')
    parser.add_argument('--output', type=pathlib.Path)
    args = parser.parse_args()
    cfg = dict(DEFAULT, **PROFILES[args.profile])
    for setting in args.set:
        key, value = setting.split('=',1)
        if key not in cfg:
            parser.error(f'unknown setting: {key}')
        cfg[key] = int(value)
    validate(cfg)
    result = Lab(cfg).run()
    result['profile'] = args.profile
    data = json.dumps(result, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(data+'\n')
    print(data)


if __name__ == '__main__':
    main()
