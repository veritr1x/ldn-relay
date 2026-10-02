/* MIT. Merge only complete, unsent negotiated batch envelopes. The stream
 * owner must not call this between ls_prepare and ls_commit. */
#ifndef RELAY_STREAM_BATCH_H
#define RELAY_STREAM_BATCH_H
#include "relay_stream.h"
#include "relay_batch.h"
static inline bool ls_batch_valid_record(void *ctx,const uint8_t *p,size_t n){
    (void)ctx;(void)p;(void)n;return true;
}
static inline bool ls_enqueue_batch(LrStream *s,const void *bytes,size_t n){
    const uint8_t *p=bytes;
    if(s->count && lr_batch_receive(p,n,ls_batch_valid_record,NULL)){
        unsigned tail=(s->head+s->count-1)%LR_QUEUE_DEPTH;
        LrMessage *m=&s->queue[tail];
        /* A partially transmitted message has an immutable total/contents. */
        if(!(tail==s->head && s->offset) && m->size+n-3<=(size_t)s->send_limit-LS_HEADER &&
           lr_batch_receive(m->bytes,m->size,ls_batch_valid_record,NULL)){
            memcpy(m->bytes+m->size,p+3,n-3);m->size+=(uint16_t)(n-3);return true;
        }
    }
    return ls_enqueue(s,bytes,n);
}
#endif
