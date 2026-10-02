/* MIT. Bounded message ownership; callers serialize access. */
#ifndef RELAY_QUEUE_H
#define RELAY_QUEUE_H
#include "relay_codec.h"
#include <string.h>
typedef struct { LrMessage items[LR_QUEUE_DEPTH]; uint64_t times[LR_QUEUE_DEPTH]; unsigned head,count; } LrQueue;
static inline bool lq_push(LrQueue *q,const void *p,size_t n,uint64_t now){
    if(!p||!n||n>LR_MAX_MESSAGE||q->count==LR_QUEUE_DEPTH)return false;
    unsigned i=(q->head+q->count)%LR_QUEUE_DEPTH;q->items[i].size=(uint16_t)n;memcpy(q->items[i].bytes,p,n);q->times[i]=now;q->count++;return true;
}
static inline bool lq_pop(LrQueue *q,LrMessage *m,uint64_t *time){
    if(!q->count)return false;
    *m=q->items[q->head];if(time)*time=q->times[q->head];q->head=(q->head+1)%LR_QUEUE_DEPTH;q->count--;return true;
}
#endif
