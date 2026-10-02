/* MIT. Platform notification backpressure, independent of stream ownership. */
#ifndef RELAY_NOTIFY_GATE_H
#define RELAY_NOTIFY_GATE_H
#include <stdbool.h>
#include <stdint.h>
/* The Switch GATT API exposes a last-event snapshot. During setup, a
 * heartbeat can overwrite the probe response before the central observes it.
 * Only the probe may be sent until the first stream write confirms setup. */
static inline bool lr_notify_negotiated(bool awaiting_stream,bool has_probe){return !awaiting_stream || has_probe;}
typedef struct {bool blocked;uint64_t since,attempt;} LrNotifyGate;
static inline void lr_notify_ready(LrNotifyGate *g){*g=(LrNotifyGate){0};}
static inline void lr_notify_result(LrNotifyGate *g,bool accepted,uint64_t now){
    if(accepted){lr_notify_ready(g);return;}
    if(!g->blocked)g->since=now;
    g->blocked=true;g->attempt=now;
}
/* The readiness callback is primary. If it stops arriving while the same
 * central still writes to us, probe at most twice a second. A failed probe
 * must not commit bytes or reset stream sequence numbers. */
static inline bool lr_notify_attempt(LrNotifyGate *g,uint64_t now,bool peerRecent){
    if(!g->blocked)return true;
    if(!peerRecent || now-g->attempt<500)return false;
    g->attempt=now;return true;
}
#endif
