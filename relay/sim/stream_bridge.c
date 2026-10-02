/* MIT licence. Native adapter for the deterministic host-only BLE laboratory. */
#include "relay_stream.h"
#include "relay_batch.h"
#include <stdlib.h>

void *lab_create(unsigned limit) {
    LrStream *s = calloc(1, sizeof(*s));
    if (s) ls_init(s, (uint16_t)limit, 0x4c414232);
    return s;
}
void lab_destroy(void *s) { free(s); }
int lab_enqueue(void *s, const void *p, size_t n) { return ls_enqueue(s,p,n); }
size_t lab_prepare(void *ptr, void *p, uint64_t now, unsigned window) {
    LrStream *s = ptr;
    /* Harness-only sender cap; retain the peer's real credit and ACK behavior. */
    uint8_t credit = s->peer_credit;
    if (s->peer_credit > window) s->peer_credit = (uint8_t)window;
    size_t n = ls_prepare(s,p,LR_MAX_FRAME,now);
    s->peer_credit = credit;
    return n;
}
void lab_commit(void *s, const void *p, size_t n, uint64_t now) { ls_commit(s,p,n,now); }
int lab_ingest(void *s, const void *p, size_t n, LrReceive receive, void *ctx, uint64_t now) {
    return ls_ingest(s,p,n,receive,ctx,now);
}
uint64_t lab_stat(LrStream *s, unsigned index) {
    switch(index) {
    case 0: return s->count;
    case 1: return s->flying;
    case 2: return s->retries;
    case 3: return s->gaps;
    case 4: return s->duplicates;
    case 5: return s->malformed;
    case 6: return s->tx_frames;
    case 7: return s->rx_frames;
    default: return 0;
    }
}
