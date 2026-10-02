/* MIT. USB buffers remain allocated until usbCommsExit cancels pending URBs. */
static uint8_t usb_read_page[LU_PAGE] __attribute__((aligned(4096)));
static uint8_t usb_write_page[LU_PAGE] __attribute__((aligned(4096)));
static void usb_worker(void *ctx){
    (void)ctx;bool reading=false,writing=false,received=false;u32 read_id=0,write_id=0,bytes=0;
    uint64_t tx=0,rx=0;
    while(!atomic_load(&stream_stop)){
        if(!reading && !received){Result rc=usbCommsReadAsync(usb_read_page,LU_PAGE,&read_id,0);if(R_SUCCEEDED(rc))reading=true;}
        if(reading && R_SUCCEEDED(eventWait(usbCommsGetReadCompletionEvent(0),0))){
            Result rc=usbCommsGetReadResult(read_id,&bytes,0);reading=false;
            if(R_FAILED(rc) || bytes!=LU_PAGE){atomic_store(&stream_failed,true);break;}received=true;
        }
        if(received){
            const uint8_t *p;size_t n;if(!lu_unpack(usb_read_page,LU_PAGE,&p,&n)){atomic_store(&stream_failed,true);break;}
            mutexLock(&stream_mutex);bool accepted=lq_push(&stream_in,p,n,now_ms());mutexUnlock(&stream_mutex);
            if(accepted){received=false;rx++;}
        }
        if(writing && R_SUCCEEDED(eventWait(usbCommsGetWriteCompletionEvent(0),0))){
            Result rc=usbCommsGetWriteResult(write_id,&bytes,0);writing=false;
            if(R_FAILED(rc)||bytes!=LU_PAGE){atomic_store(&stream_failed,true);break;}tx++;
            mutexLock(&stream_mutex);LrMessage done;lq_pop(&stream_out,&done,NULL);mutexUnlock(&stream_mutex);
        }
        if(!writing){
            mutexLock(&stream_mutex);bool available=stream_out.count>0;
            if(available){LrMessage *m=&stream_out.items[stream_out.head];lu_pack(usb_write_page,m->bytes,m->size);}
            mutexUnlock(&stream_mutex);
            if(available && R_SUCCEEDED(usbCommsWriteAsync(usb_write_page,LU_PAGE,&write_id,0)))writing=true;
        }
        mutexLock(&stream_mutex);stream_tx=tx;stream_rx=rx;mutexUnlock(&stream_mutex);
        svcSleepThread(500000LL);
    }
}
