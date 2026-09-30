#include "relay_codec.h"
#include "relay_batch.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { unsigned next, received, blocked; } Sink;
static bool sink(void *ctx,const uint8_t *p,size_t n) {
    Sink *s=ctx;
    if(s->blocked){s->blocked--;return false;}
    assert(n==1+(s->next*131)%LR_MAX_MESSAGE);
    for(size_t i=0;i<n;i++)assert(p[i]==(uint8_t)(s->next+i*17));
    s->next++;s->received++;return true;
}
static void queue(LrCodec *c,unsigned i) {
    uint8_t p[LR_MAX_MESSAGE];size_t n=1+(i*131)%sizeof(p);
    for(size_t j=0;j<n;j++)p[j]=(uint8_t)(i+j*17);
    assert(lr_enqueue(c,p,n));
}
static void exchange(uint16_t limit, bool wrap) {
    static LrCodec a,b;lr_init(&a,limit);lr_init(&b,limit);
    if(wrap){a.tx_seq=b.tx_seq=65520;a.rx_ack=b.rx_ack=65519;}
    Sink sa={0},sb={.blocked=2};
    uint8_t ab[LR_MAX_FRAME],ba[LR_MAX_FRAME];
    unsigned na=0,nb=0;
    for(unsigned step=0;step<400000 && (sa.received<120 || sb.received<120);step++){
        if(na<120 && a.count<LR_QUEUE_DEPTH)queue(&a,na++);
        if(nb<120 && b.count<LR_QUEUE_DEPTH)queue(&b,nb++);
        size_t an=lr_frame(&a,ab,sizeof(ab));assert(an);
        size_t bn=lr_frame(&b,ba,sizeof(ba));assert(bn);
        /* Simulate lost frames, repeated reads/writes and damaged frames. */
        if(step%11) {
            if(step%19==0){ab[an-1]^=0x40;assert(!lr_ingest(&b,ab,an,sink,&sb));ab[an-1]^=0x40;}
            lr_ingest(&b,ab,an,sink,&sb);
            if(step%7==0)lr_ingest(&b,ab,an,sink,&sb);
        }
        if(step%13){lr_ingest(&a,ba,bn,sink,&sa);if(step%5==0)lr_ingest(&a,ba,bn,sink,&sa);}
    }
    assert(sa.received==120 && sb.received==120);
    assert(a.malformed_frames+b.malformed_frames>0);
    assert(a.duplicate_frames+b.duplicate_frames>0);
}
static bool batch_sink(void *ctx,const uint8_t *p,size_t n) {
    unsigned *count=ctx;assert(n==71 && p[0]==RL_UDP && p[10]==*count);++*count;return true;
}
static bool batch_envelope(void *ctx,const uint8_t *p,size_t n) {
    return lr_batch_receive(p,n,batch_sink,ctx);
}
static void batches(void) {
    LrBatch batch;lr_batch_init(&batch,500-LR_HEADER_SIZE);
    uint8_t packet[71]={RL_UDP};unsigned count=0;
    for(unsigned i=0;i<6;++i){packet[10]=(uint8_t)i;assert(lr_batch_add(&batch,packet,sizeof(packet)));}
    size_t full=batch.size;assert(!lr_batch_add(&batch,packet,sizeof(packet)));assert(batch.size==full);
    assert(batch.size+LR_HEADER_SIZE<=500);
    uint8_t malformed[LR_MAX_MESSAGE];memcpy(malformed,batch.bytes,full);
    lr_put16(malformed+3+5*73,72);assert(!lr_batch_receive(malformed,full,batch_sink,&count));assert(count==0);
    memcpy(malformed,batch.bytes,full);malformed[5+5*73]=RL_BATCH;
    assert(!lr_batch_receive(malformed,full,batch_sink,&count));assert(count==0);
    static LrCodec a,b;lr_init(&a,500);lr_init(&b,500);assert(lr_enqueue(&a,batch.bytes,batch.size));
    uint8_t wire[LR_MAX_FRAME];size_t n=lr_frame(&a,wire,sizeof(wire));
    assert(lr_ingest(&b,wire,n,batch_envelope,&count));assert(count==6);
    assert(lr_ingest(&b,wire,n,batch_envelope,&count));assert(count==6);
    for(unsigned limit=4;limit<500;++limit){
        lr_batch_init(&batch,limit);
        if(lr_batch_add(&batch,packet,sizeof(packet)))assert(batch.size<=limit);
    }
    puts("PASS: six UDP events in one BLE frame, order, malformed/nested envelopes, atomic validation, duplicate suppression, MTU bounds");
}
int main(void){
    batches();
    exchange(20,false);exchange(182,false);exchange(500,false);exchange(182,true);
    static LrCodec c;lr_init(&c,500);
    uint8_t p[LR_MAX_MESSAGE+1]={0};
    assert(!lr_enqueue(&c,p,0));assert(!lr_enqueue(&c,p,sizeof(p)));
    for(unsigned i=0;i<LR_QUEUE_DEPTH;i++)assert(lr_enqueue(&c,p,1));
    assert(!lr_enqueue(&c,p,1));
    for(size_t n=0;n<sizeof(p);n++)assert(!lr_ingest(&c,p,n,sink,NULL));
    lr_init(&c,20);assert(c.count==0 && c.rx_ack==0 && c.tx_seq==1);
    puts("PASS: bidirectional fragmentation, loss, duplicates, CRC, backpressure, queue bounds, sequence wrap and reset");
}
