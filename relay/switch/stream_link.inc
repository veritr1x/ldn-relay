#include "relay_stream_batch.h"
#include "relay_recovery.h"
/* MIT. Included after ble_link.inc. BLE owns its stream; main owns LDN/UDP. */
static LrStream stream;
static LrQueue stream_in,stream_out;
static Mutex stream_mutex;
static atomic_bool stream_stop,stream_failed;
static atomic_uint stream_pending,stream_tuning;
static bool stream_mode;
static Thread stream_thread;
static uint64_t stream_tx,stream_rx,stream_retry,stream_ack_max,stream_ack_avg,stream_queue_age;
static uint64_t stream_wakes,stream_events,stream_max_drain;
static uint64_t stream_data_frames,stream_ack_frames,stream_tx_bytes;
static unsigned stream_unsent,stream_flying,stream_recovery_reads,stream_recovery_timeouts;
static bool stream_pulling;
#ifndef LR_SWITCH_WRITE_PACE_MS
#define LR_SWITCH_WRITE_PACE_MS 5
#endif
static uint64_t stream_poll_max_us,stream_read_max_us,stream_write_max_us,stream_read_errors,stream_write_errors,stream_empty_events;
static uint64_t now_ms(void){return armTicksToNs(armGetSystemTick())/1000000;}
static unsigned transport_depth(void){
    if(!stream_mode)return codec.count;
    mutexLock(&stream_mutex);unsigned n=stream_out.count;mutexUnlock(&stream_mutex);return n+atomic_load(&stream_pending);
}
static bool transport_enqueue(const void *p,size_t n){
    if(!stream_mode)return lr_enqueue(&codec,p,n);
    mutexLock(&stream_mutex);bool ok=lq_push(&stream_out,p,n,now_ms());mutexUnlock(&stream_mutex);return ok;
}
static bool stream_receive(void *ctx,const uint8_t *p,size_t n){
    (void)ctx;mutexLock(&stream_mutex);bool ok=lq_push(&stream_in,p,n,now_ms());mutexUnlock(&stream_mutex);return ok;
}
static void stream_worker(void *ctx){
    uint64_t previous_poll=0,write_started=0;bool write_pending=false,read_after_write=false;
    stream_data_frames=stream_ack_frames=stream_tx_bytes=0;
    mutexLock(&stream_mutex);stream_poll_max_us=stream_read_max_us=stream_write_max_us=stream_read_errors=stream_write_errors=stream_empty_events=0;mutexUnlock(&stream_mutex);
    BleSession *b=ctx;uint64_t last_rx=now_ms(),next_write=0;
    LrRecovery recovery;lr_recovery_init(&recovery,b->read_recovery,last_rx);
    BtdrvGattId service={.instance_id=b->service.instance_id,.uuid=b->service.attr.uuid};
    BtdrvGattId attr={.instance_id=b->characteristic.instance_id,.uuid=b->characteristic.attr.uuid};
    while(!atomic_load(&stream_stop)){
        unsigned tuning=atomic_load(&stream_tuning);
        if(tuning){ls_set_frame(&stream,tuning>>8);ls_set_window(&stream,tuning&255);}
        uint64_t poll=armTicksToNs(armGetSystemTick())/1000;
        mutexLock(&stream_mutex);if(previous_poll && poll-previous_poll>stream_poll_max_us)stream_poll_max_us=poll-previous_poll;mutexUnlock(&stream_mutex);previous_poll=poll;
        mutexLock(&stream_mutex);
        while(stream_out.count && stream.count<LR_QUEUE_DEPTH){
            LrMessage m;uint64_t stamp;lq_pop(&stream_out,&m,&stamp);
            uint64_t age=now_ms()-stamp;if(age>stream_queue_age)stream_queue_age=age;
            ls_enqueue_batch(&stream,m.bytes,m.size);
        }
        ls_ready(&stream,stream_in.count<LR_QUEUE_DEPTH);mutexUnlock(&stream_mutex);
        if(R_SUCCEEDED(eventWait(&b->gatt_event,0))){
            unsigned drained=0;
            /* bt exposes a last-event snapshot, not a dequeue API. */
            for(unsigned i=0;i<1;i++){
                BtdrvLeEventInfo e={0};BtdrvBleEventType type=0;
                uint64_t begin=armGetSystemTick();Result read_rc=btGetLeEventInfo(&e,sizeof(e),&type);
                uint64_t read_us=armTicksToNs(armGetSystemTick()-begin)/1000;
                mutexLock(&stream_mutex);if(read_us>stream_read_max_us)stream_read_max_us=read_us;if(R_FAILED(read_rc))stream_read_errors++;else if(!e.size)stream_empty_events++;mutexUnlock(&stream_mutex);
                if(R_FAILED(read_rc))break;
                if(!e.size){if(type==8 && relevant(&e,&b->char_uuid))write_pending=false;break;}
                drained++;
                if(type==8 && relevant(&e,&b->char_uuid) && e.size>=LS_HEADER && e.size<=LR_MAX_FRAME){
                    uint8_t plain[LR_MAX_FRAME];size_t plain_n=lp_unwrap(&b->session,e.data,e.size,plain,sizeof(plain));
                    if(plain_n && ls_ingest(&stream,plain,plain_n,stream_receive,NULL,now_ms())){last_rx=now_ms();lr_recovery_received(&recovery,last_rx);
                        if(e.data[2]=='N' && recovery.pulling){lr_recovery_init(&recovery,b->read_recovery,last_rx);read_after_write=false;}}
                }
            }
            mutexLock(&stream_mutex);stream_wakes++;stream_events+=drained;if(drained>stream_max_drain)stream_max_drain=drained;mutexUnlock(&stream_mutex);
        }
        for(unsigned i=0;i<LS_WINDOW+1;i++){
            if(now_ms()<next_write || (recovery.pulling && (recovery.pending || write_pending || read_after_write)))break;
            uint8_t bytes[LR_MAX_FRAME];size_t n=ls_prepare(&stream,bytes,sizeof(bytes),now_ms());if(!n)break;
            uint8_t wire[LR_MAX_FRAME];size_t wire_n=lp_wrap(&b->session,bytes,n,wire,sizeof(wire));if(!wire_n){atomic_store(&stream_failed,true);break;}
            uint64_t begin=armGetSystemTick();
            Result rc=btLeClientWriteCharacteristic(b->handle,b->service.primary_service,&service,&attr,wire,wire_n,0,false);
            uint64_t write_us=armTicksToNs(armGetSystemTick()-begin)/1000;
            mutexLock(&stream_mutex);if(write_us>stream_write_max_us)stream_write_max_us=write_us;if(R_FAILED(rc))stream_write_errors++;mutexUnlock(&stream_mutex);
            if(R_FAILED(rc)){next_write=now_ms()+5;break;} /* Retry without spinning. */
            mutexLock(&stream_mutex);stream_tx_bytes+=n;if(bytes[3])stream_data_frames++;else stream_ack_frames++;mutexUnlock(&stream_mutex);
            ls_commit(&stream,bytes,n,now_ms());next_write=now_ms()+LR_SWITCH_WRITE_PACE_MS;
            if(recovery.pulling){write_pending=read_after_write=true;write_started=now_ms();break;}
        }
        if(write_pending && now_ms()-write_started>=500)write_pending=false;
        if(!write_pending && lr_recovery_due(&recovery,now_ms())){
            Result rc=btLeClientReadCharacteristic(b->handle,b->service.primary_service,&service,&attr,0);
            lr_recovery_requested(&recovery,R_SUCCEEDED(rc),now_ms());if(R_SUCCEEDED(rc))read_after_write=false;
        }
        atomic_store(&stream_pending,stream.count+stream.flying);
        mutexLock(&stream_mutex);stream_recovery_reads=recovery.reads;stream_recovery_timeouts=recovery.timeouts;stream_pulling=recovery.pulling;stream_unsent=stream.count;stream_flying=stream.flying;stream_tx=stream.tx_frames;stream_rx=stream.rx_frames;stream_retry=stream.retries;
        stream_ack_max=stream.ack_ms_max;stream_ack_avg=stream.ack_samples?stream.ack_ms_sum/stream.ack_samples:0;mutexUnlock(&stream_mutex);
        if(now_ms()-last_rx>10000){atomic_store(&stream_failed,true);break;}
        svcSleepThread(1000000LL);
    }
}
