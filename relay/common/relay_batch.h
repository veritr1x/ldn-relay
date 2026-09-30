/* MIT licence: see ../../LICENSE. Optional, negotiated event batching. */
#ifndef RELAY_BATCH_H
#define RELAY_BATCH_H
#include "relay_protocol.h"
#include <string.h>
typedef struct { uint8_t bytes[LR_MAX_MESSAGE];size_t size,limit; } LrBatch;
static inline void lr_batch_init(LrBatch *b,size_t limit) {
    memset(b,0,sizeof(*b));b->bytes[0]=RL_BATCH;b->size=3;
    b->limit=limit>LR_MAX_MESSAGE?LR_MAX_MESSAGE:limit;
}
static inline bool lr_batch_add(LrBatch *b,const uint8_t *p,size_t n) {
    if(!p || n<3 || p[0]==RL_BATCH || b->size+2+n>b->limit)return false;
    lr_put16(b->bytes+b->size,(uint16_t)n);memcpy(b->bytes+b->size+2,p,n);b->size+=2+n;return true;
}
/* Validate the whole envelope before delivery, including every nested length.
 * Nested batches are forbidden. The consumer must accept each valid message. */
static inline bool lr_batch_receive(const uint8_t *p,size_t n,LrReceive receive,void *ctx) {
    if(!p || !receive || n<8 || n>LR_MAX_MESSAGE || p[0]!=RL_BATCH || p[1] || p[2])return false;
    for(size_t at=3;at<n;) {
        if(n-at<2)return false;
        size_t length=lr_get16(p+at);at+=2;
        if(length<3 || length>n-at || p[at]==RL_BATCH)return false;
        at+=length;
    }
    for(size_t at=3;at<n;) {
        size_t length=lr_get16(p+at);at+=2;
        if(!receive(ctx,p+at,length))return false;
        at+=length;
    }
    return true;
}
#endif
