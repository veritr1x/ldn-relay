/* MIT licence. Version 2: bounded, independently pumped, cumulative ACK stream. */
#ifndef RELAY_STREAM_H
#define RELAY_STREAM_H
#include "relay_codec.h"
#define LS_VERSION 2
#define LS_HEADER 24
#define LS_WINDOW 8
#define LS_RETRY_MS 150
typedef struct { uint8_t bytes[LR_MAX_FRAME]; size_t size; uint64_t sent; bool retried; } LsFlight;
typedef struct {
    LrMessage queue[LR_QUEUE_DEPTH]; unsigned head,count,offset;
    LsFlight flight[LS_WINDOW]; unsigned first,flying,replay;
    uint16_t send_limit,limit,next,acked,received,used,total;
    uint32_t session; uint8_t peer_credit,send_window; bool ready,ack_dirty;
    uint8_t assembly[LR_MAX_MESSAGE]; uint64_t last_sent,progress,ack_due;unsigned ack_delay_ms;
    uint64_t tx_frames,rx_frames,retries,gaps,duplicates,blocked,malformed;
    uint64_t ack_samples,ack_ms_sum,ack_ms_max,retry_floor_ms;
} LrStream;
void ls_init(LrStream *,uint16_t,uint32_t);
bool ls_enqueue(LrStream *,const void *,size_t);
void ls_set_frame(LrStream *,unsigned);
void ls_ready(LrStream *,bool);
void ls_set_window(LrStream *,unsigned);
/* Preparing never retires messages or marks a frame sent. Commit only after
 * the platform accepts the write/notification. All access has one owner. */
size_t ls_prepare(LrStream *,void *,size_t,uint64_t);
void ls_commit(LrStream *,const void *,size_t,uint64_t);
bool ls_ingest(LrStream *,const void *,size_t,LrReceive,void *,uint64_t);
#endif
