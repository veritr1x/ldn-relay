/* MIT. Deterministic transport tests, not measurements of any Bluetooth radio. */
#include "relay_stream.h"
#include "relay_recovery.h"
#include "relay_notify_gate.h"
#include "relay_admission.h"
#include "relay_batch.h"
#include "relay_queue.h"
#include "relay_stream_batch.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {unsigned next,blocked;uint64_t now,max_age,sum;uint32_t ages[2001];bool load;} Sink;
static bool packet(void *ctx,const uint8_t *p,size_t n){
    Sink *s=ctx;assert(n==130);unsigned seq=lr_get32(p+3);assert(seq==s->next++);
    for(size_t i=15;i<n;i++)assert(p[i]==(uint8_t)(seq+i*17));
    uint64_t age=s->now-lr_get64(p+7);s->sum+=age;if(age>s->max_age)s->max_age=age;s->ages[age>2000?2000:age]++;return true;
}
static bool receive(void *ctx,const uint8_t *p,size_t n){
    Sink *s=ctx;if(s->blocked){s->blocked--;return false;}
    if(s->load)return lr_batch_receive(p,n,packet,ctx);
    size_t expected=1+(s->next*131)%LR_MAX_MESSAGE;assert(n==expected);
    for(size_t i=0;i<n;i++)assert(p[i]==(uint8_t)(s->next+i*17));s->next++;return true;
}
static void queue_message(LrStream *s,unsigned seq){uint8_t p[LR_MAX_MESSAGE];size_t n=1+(seq*131)%LR_MAX_MESSAGE;for(size_t i=0;i<n;i++)p[i]=(uint8_t)(seq+i*17);assert(ls_enqueue(s,p,n));}
static void adversity(unsigned limit,bool wrap){
    static LrStream a,b;ls_init(&a,limit,42);ls_init(&b,limit,42);
    if(wrap){a.next=b.next=65520;a.received=b.received=a.acked=b.acked=65519;}
    Sink sa={.blocked=2},sb={.blocked=3};unsigned na=0,nb=0,sends=0;
    uint8_t ab[500],ba[500];
    /* Exponential backoff needs a larger simulated deadline under repeated loss. */
    for(uint64_t now=1;now<5000000 && (sa.next<150 || sb.next<150 || a.flying || b.flying);now++){
        if(na<150 && a.count<LR_QUEUE_DEPTH)queue_message(&a,na++);
        if(nb<150 && b.count<LR_QUEUE_DEPTH)queue_message(&b,nb++);
        if(now%17==0){ls_ready(&a,true);ls_ready(&b,true);}
        size_t an=ls_prepare(&a,ab,sizeof(ab),now),bn=ls_prepare(&b,ba,sizeof(ba),now);
        if(an){ls_commit(&a,ab,an,now);sends++;if(sends%13){if(sends%19==0){ab[an-1]^=64;assert(!ls_ingest(&b,ab,an,receive,&sb,now));ab[an-1]^=64;}ls_ingest(&b,ab,an,receive,&sb,now);if(sends%7==0)ls_ingest(&b,ab,an,receive,&sb,now);}}
        if(bn){ls_commit(&b,ba,bn,now);if(now%11)ls_ingest(&a,ba,bn,receive,&sa,now);}
    }
    assert(sa.next==150 && sb.next==150 && !a.flying && !b.flying);assert(a.retries+b.retries>0);
    printf("PASS stream MTU=%u wrap=%u: loss, corruption, duplicates, backpressure, fragmentation, cumulative ACK recovery\n",limit,wrap);
}
typedef struct {uint64_t due;unsigned target;size_t n;uint8_t p[500];} Event;
static Event events[4096];static unsigned event_head,event_count;
static void schedule(unsigned target,const uint8_t *p,size_t n,uint64_t due){assert(event_count<4096);Event *e=&events[(event_head+event_count++)%4096];e->target=target;e->due=due;e->n=n;memcpy(e->p,p,n);}
static void load(unsigned rate){
    static LrStream peers[2];Sink sink[2]={{.load=true},{.load=true}};LrBatch batches[2];unsigned generated[2]={0};
    for(unsigned d=0;d<2;d++){ls_init(&peers[d],500,42);lr_batch_init(&batches[d],500-LS_HEADER);}event_head=event_count=0;
    const unsigned duration=60000,target=rate*60;
    for(uint64_t now=0;now<duration+3000;now++){
        for(unsigned d=0;d<2;d++){
            unsigned expected=now<duration?(unsigned)(now*rate/1000):target;
            while(generated[d]<expected){
                unsigned seq=generated[d]++;uint8_t p[130]={RL_UDP};lr_put32(p+3,seq);lr_put64(p+7,now);
                for(size_t j=15;j<sizeof(p);j++)p[j]=(uint8_t)(seq+j*17);
                if(!lr_batch_add(&batches[d],p,sizeof(p))){assert(ls_enqueue(&peers[d],batches[d].bytes,batches[d].size));lr_batch_init(&batches[d],500-LS_HEADER);assert(lr_batch_add(&batches[d],p,sizeof(p)));}
            }
            if(now%10==0 && batches[d].size>3){assert(ls_enqueue(&peers[d],batches[d].bytes,batches[d].size));lr_batch_init(&batches[d],500-LS_HEADER);}
        }
        while(event_count && events[event_head].due<=now){Event e=events[event_head];event_head=(event_head+1)%4096;event_count--;sink[e.target].now=now;assert(ls_ingest(&peers[e.target],e.p,e.n,receive,&sink[e.target],now));}
        /* Model assumption: three ATT transfers per direction every 15 ms,
         * delivered 15 ms later. This is not a measured platform capability. */
        if(now%15==0)for(unsigned d=0;d<2;d++)for(unsigned i=0;i<3;i++){
            uint8_t frame[500];size_t n=ls_prepare(&peers[d],frame,sizeof(frame),now);if(!n)break;
            ls_commit(&peers[d],frame,n,now);schedule(1-d,frame,n,now+15);
        }
    }
    for(unsigned d=0;d<2;d++)assert(sink[d].next==target && sink[d].max_age<100 && !peers[d].count && !peers[d].flying);
    printf("PASS SIMULATED %u packets/s EACH way for 60 s: %u delivered each, no overflow, maximum delay=%llu/%llu ms (assumed link, NOT hardware)\n",rate,target,(unsigned long long)sink[0].max_age,(unsigned long long)sink[1].max_age);
}
static bool slow_receive(void *ctx,const uint8_t *p,size_t n){(void)p;assert(n==1);(*(unsigned *)ctx)++;return true;}
static void slow_progress(void){
    static LrStream a,b;ls_init(&a,500,88);ls_init(&b,500,88);
    uint8_t frames[8][500],ack[500];size_t sizes[8];unsigned received=0;
    for(unsigned i=0;i<8;i++){uint8_t p=(uint8_t)i;assert(ls_enqueue(&a,&p,1));sizes[i]=ls_prepare(&a,frames[i],500,0);assert(sizes[i]);ls_commit(&a,frames[i],sizes[i],0);}
    /* Eight accepted platform writes serialize over a slow but lossless link.
     * ACKs keep arriving every 100 ms. They must not trigger a retry storm. */
    for(uint64_t t=1;t<=850;t++){
        if(t%100==0){unsigned i=(unsigned)(t/100-1);assert(ls_ingest(&b,frames[i],sizes[i],slow_receive,&received,t));size_t n=ls_prepare(&b,ack,500,t);assert(n);ls_commit(&b,ack,n,t);assert(ls_ingest(&a,ack,n,slow_receive,NULL,t));}
        uint8_t f[500];size_t n=ls_prepare(&a,f,500,t);if(n){assert(!f[3]);ls_commit(&a,f,n,t);}
    }
    assert(received==8 && !a.flying && !a.retries);puts("PASS slow lossless platform queue: advancing ACKs prevent premature retransmissions");
}
static void independent_receive_credit(void){
    LrStream sender,receiver;ls_init(&sender,182,91);ls_init(&receiver,182,91);
    uint8_t frame[500],payload=1;unsigned received=0;
    /* An unrelated full outgoing queue must not stop receiving ordinary data. */
    ls_ready(&receiver,lr_receive_ready(0,32,0));
    size_t n=ls_prepare(&receiver,frame,500,0);ls_commit(&receiver,frame,n,0);
    assert(ls_ingest(&sender,frame,n,slow_receive,NULL,0));
    assert(ls_enqueue(&sender,&payload,1));n=ls_prepare(&sender,frame,500,1);
    assert(n>LS_HEADER);ls_commit(&sender,frame,n,1);
    assert(ls_ingest(&receiver,frame,n,slow_receive,&received,1));assert(received==1);
    /* A blocked echo batch needs all reply slots before reopening credits. */
    assert(!lr_receive_ready(0,31,2));assert(lr_receive_ready(0,30,2));
    assert(!lr_receive_ready(64,0,0));assert(!lr_receive_ready(0,32,1));
    puts("PASS independent RX credits with full TX queue, bounded echo reply reservation, full receive queue");
}
static void sender_window(void){
    LrStream s;ls_init(&s,500,7);ls_set_window(&s,1);uint8_t p[3]={1,2,3},f[500];
    assert(ls_enqueue(&s,p,3) && ls_enqueue(&s,p,3));
    size_t n=ls_prepare(&s,f,500,0);assert(f[3]);ls_commit(&s,f,n,0);
    assert(!ls_prepare(&s,f,500,1));s.ack_dirty=true;
    n=ls_prepare(&s,f,500,1);assert(n==LS_HEADER && !f[3]);
    ls_set_window(&s,2);n=ls_prepare(&s,f,500,2);assert(n>LS_HEADER && f[3]);
    ls_set_window(&s,0);assert(s.send_window==1);ls_set_window(&s,99);assert(s.send_window==LS_WINDOW);
    puts("PASS sender window bounds and ACKs while data window full");
}
static void callback_stall(void){
    LrStream s;ls_init(&s,500,42);s.ack_ms_max=200;uint8_t payload[3]={1,2,3},frame[500];
    assert(ls_enqueue(&s,payload,sizeof(payload)));
    size_t n=ls_prepare(&s,frame,sizeof(frame),0);ls_commit(&s,frame,n,0);
    /* A 300 ms callback stall must not cause a premature whole-window replay. */
    n=ls_prepare(&s,frame,sizeof(frame),300);assert(n==LS_HEADER && !frame[3]);
    n=ls_prepare(&s,frame,sizeof(frame),450);assert(n>LS_HEADER && frame[3]);
    ls_commit(&s,frame,n,450);assert(s.retries==1);
    puts("PASS callback stall tolerance with bounded retry after silence");
}
/* A link can slow after the only clean RTT samples were collected. Retries
 * must back off even when Karn's rule excludes every subsequent RTT sample. */
static void retry_backoff(void){
    LrStream a,b;ls_init(&a,500,42);ls_init(&b,500,42);a.ack_ms_max=68;
    uint8_t p=1,f[500],ack[500];unsigned received=0;
    assert(ls_enqueue(&a,&p,1));size_t n=ls_prepare(&a,f,500,0);ls_commit(&a,f,n,0);
    unsigned retries=0;
    for(uint64_t t=1;t<=1500;t++){
        n=ls_prepare(&a,f,500,t);if(!n)continue;
        if(f[3])retries++;
        ls_commit(&a,f,n,t); /* Platform accepts, but delivery is delayed. */
    }
    assert(retries<=3); /* Old fixed 186 ms timer sends eight copies. */
    n=ls_prepare(&a,f,500,4000);assert(n>LS_HEADER);ls_commit(&a,f,n,4000);
    assert(ls_ingest(&b,f,n,slow_receive,&received,4001));
    n=ls_prepare(&b,ack,500,4002);ls_commit(&b,ack,n,4002);
    assert(ls_ingest(&a,ack,n,slow_receive,NULL,4003));assert(!a.flying && received==1);
    /* Ambiguous ACK must not reset the backoff to the old fast-link estimate. */
    assert(ls_enqueue(&a,&p,1));n=ls_prepare(&a,f,500,4004);ls_commit(&a,f,n,4004);
    n=ls_prepare(&a,ack,500,4300);assert(n==LS_HEADER && !ack[3]);
    /* A fresh, unambiguous RTT sample permits normal fast recovery again. */
    assert(ls_ingest(&b,f,LS_HEADER+1,slow_receive,&received,4301));
    n=ls_prepare(&b,ack,500,4302);ls_commit(&b,ack,n,4302);
    assert(ls_ingest(&a,ack,n,slow_receive,NULL,4303));
    assert(ls_enqueue(&a,&p,1));n=ls_prepare(&a,f,500,4304);ls_commit(&a,f,n,4304);
    n=ls_prepare(&a,f,500,4952);assert(n>LS_HEADER);
    puts("PASS abrupt link slowdown: bounded retries, ambiguous ACK preserves backoff, clean RTT restores recovery");
}
static void ack_coalescing(void){
    LrStream a,b;ls_init(&a,500,12);ls_init(&b,500,12);b.ack_delay_ms=10;
    uint8_t data=1,f[500],ack[500];unsigned count=0;
    size_t n=ls_prepare(&b,ack,500,0);ls_commit(&b,ack,n,0);
    assert(ls_enqueue(&a,&data,1));n=ls_prepare(&a,f,500,1);ls_commit(&a,f,n,1);
    assert(ls_ingest(&b,f,n,slow_receive,&count,1));assert(!ls_prepare(&b,ack,500,10));
    /* Another arrival does not extend the first ACK deadline. */
    assert(ls_enqueue(&a,&data,1));n=ls_prepare(&a,f,500,5);ls_commit(&a,f,n,5);
    assert(ls_ingest(&b,f,n,slow_receive,&count,5));
    n=ls_prepare(&b,ack,500,11);assert(n==LS_HEADER && lr_get16(ack+6)==2);ls_commit(&b,ack,n,11);
    assert(ls_ingest(&a,ack,n,slow_receive,&count,11));assert(!a.flying);
    /* Outbound data carries the fresh ACK immediately, without delay. */
    assert(ls_enqueue(&a,&data,1));n=ls_prepare(&a,f,500,12);ls_commit(&a,f,n,12);assert(ls_ingest(&b,f,n,slow_receive,&count,12));
    assert(ls_enqueue(&b,&data,1));n=ls_prepare(&b,ack,500,12);assert(n>LS_HEADER && lr_get16(ack+6)==3);
    puts("PASS bounded ACK coalescing: cumulative ACK, fixed deadline, immediate piggyback");
}

static bool packed_record(void *ctx,const uint8_t *p,size_t n){
    unsigned *next=ctx;assert(n==120 && p[0]==RL_UDP && lr_get16(p+1)==(*next)++);return true;
}
static bool packed_receive(void *ctx,const uint8_t *p,size_t n){return lr_batch_receive(p,n,packed_record,ctx);}
static void queued_batch_packing(void){
    static LrStream a,b;ls_init(&a,500,51);ls_init(&b,500,51);
    a.head=LR_QUEUE_DEPTH-2;unsigned delivered=0;LrBatch batch;
    for(unsigned i=0;i<24;i++){
        uint8_t p[120]={RL_UDP};lr_put16(p+1,i);lr_batch_init(&batch,476);assert(lr_batch_add(&batch,p,sizeof(p)));
        assert(ls_enqueue_batch(&a,batch.bytes,batch.size));
    }
    assert(a.count==8); /* Previously 24 one-record frames; now eight triples. */
    for(uint64_t t=1;t<100 && (a.count || a.flying);t++){
        uint8_t f[500];size_t n=ls_prepare(&a,f,sizeof(f),t);
        if(n){ls_commit(&a,f,n,t);assert(ls_ingest(&b,f,n,packed_receive,&delivered,t));}
        n=ls_prepare(&b,f,sizeof(f),t);
        if(n){ls_commit(&b,f,n,t);assert(ls_ingest(&a,f,n,NULL,NULL,t));}
    }
    assert(delivered==24 && !a.count && !a.flying);
    /* Never extend a message after its first fragment is committed. */
    ls_init(&a,64,51);uint8_t big[120]={RL_UDP},f[500];lr_batch_init(&batch,476);assert(lr_batch_add(&batch,big,120));
    assert(ls_enqueue_batch(&a,batch.bytes,batch.size));size_t n=ls_prepare(&a,f,sizeof(f),0);ls_commit(&a,f,n,0);
    unsigned original=a.queue[a.head].size;assert(a.offset);a.limit=500;
    assert(ls_enqueue_batch(&a,batch.bytes,batch.size));assert(a.count==2 && a.queue[a.head].size==original);
    /* A malformed envelope cannot corrupt an earlier valid queued batch. */
    ls_init(&a,500,51);assert(ls_enqueue_batch(&a,batch.bytes,batch.size));original=a.queue[a.head].size;
    uint8_t bad[8]={RL_BATCH,0,0,255,255,RL_UDP,0,0};assert(ls_enqueue_batch(&a,bad,sizeof(bad)));
    assert(a.count==2 && a.queue[a.head].size==original);
    /* A full queue can still absorb a record into an unsent tail. */
    ls_init(&a,500,51);for(unsigned i=0;i<LR_QUEUE_DEPTH;i++)assert(ls_enqueue(&a,batch.bytes,batch.size));
    assert(ls_enqueue_batch(&a,batch.bytes,batch.size));assert(a.count==LR_QUEUE_DEPTH);
    puts("PASS queued batch packing: 24 records in eight frames, ordered, wrap, immutable partial messages, malformed isolation, full queue");
}

static void frame_cap_sweep(void){
    const unsigned caps[]={182,320,500},windows[]={1,3,6};
    for(unsigned c=0;c<3;c++)for(unsigned w=0;w<3;w++){
        static LrStream a,b;ls_init(&a,500,7);ls_init(&b,500,7);
        ls_set_frame(&a,caps[c]);ls_set_frame(&b,caps[c]);ls_set_window(&a,windows[w]);ls_set_window(&b,windows[w]);
        Sink sink={0};queue_message(&a,0);queue_message(&a,1);queue_message(&a,2);queue_message(&a,3);
        for(uint64_t t=1;t<1000 && (a.count || a.flying);t++){
            uint8_t frame[500];size_t n=ls_prepare(&a,frame,sizeof(frame),t);
            if(n){assert(n<=caps[c]);ls_commit(&a,frame,n,t);assert(a.flying<=windows[w]);assert(ls_ingest(&b,frame,n,receive,&sink,t));}
            n=ls_prepare(&b,frame,sizeof(frame),t);if(n){assert(n<=caps[c]);ls_commit(&b,frame,n,t);assert(ls_ingest(&a,frame,n,NULL,NULL,t));}
        }
        assert(sink.next==4 && !a.flying && !a.count);
        assert(a.limit==500 && b.limit==500); /* Receive MTU remains negotiated. */
        ls_set_frame(&a,1);assert(a.send_limit==64);ls_set_frame(&a,600);assert(a.send_limit==500);
    }
    puts("PASS nine frame/window settings: capped frames, ordered reassembly, unchanged receive MTU, bounded window");
}
static void read_recovery(void){
    LrRecovery policy;lr_recovery_init(&policy,false,0);
    assert(!lr_recovery_due(&policy,10000));
    lr_recovery_init(&policy,true,0);assert(!lr_recovery_due(&policy,499));
    assert(lr_recovery_due(&policy,500));lr_recovery_requested(&policy,false,500);
    assert(!lr_recovery_due(&policy,599));assert(lr_recovery_due(&policy,600));
    lr_recovery_requested(&policy,true,600);assert(!lr_recovery_due(&policy,1099));
    assert(lr_recovery_due(&policy,1100) && policy.timeouts==1);
    /* Simulate a permanent notification stall after the platform accepted and
       lost its first frame. Reads must replay it without resetting the stream.
       A late notification is injected after the read to test deduplication. */
    static LrStream phone,console;ls_init(&phone,500,123);ls_init(&console,500,123);
    Sink sink={0};for(unsigned i=0;i<12;i++)queue_message(&phone,i);
    uint8_t lost[500],frame[500];size_t lost_n=ls_prepare(&phone,lost,500,0);
    assert(lost_n);ls_commit(&phone,lost,lost_n,0);
    lr_recovery_init(&policy,true,0);bool switched=false,late=false;
    for(uint64_t t=1;t<10000 && (phone.count || phone.flying);t++){
        if(lr_recovery_due(&policy,t)){
            lr_recovery_requested(&policy,true,t);
            if(!switched){phone.replay=phone.flying;switched=true;}
            size_t n=ls_prepare(&phone,frame,500,t);
            if(!n){phone.ack_dirty=true;phone.ack_due=t;n=ls_prepare(&phone,frame,500,t);}
            assert(n);ls_commit(&phone,frame,n,t);
            assert(ls_ingest(&console,frame,n,receive,&sink,t));lr_recovery_received(&policy,t);
            if(!late){assert(ls_ingest(&console,lost,lost_n,receive,&sink,t));late=true;}
        }
        size_t n=ls_prepare(&console,frame,500,t);
        if(n){ls_commit(&console,frame,n,t);assert(ls_ingest(&phone,frame,n,NULL,NULL,t));}
    }
    assert(switched && late && sink.next==12 && !phone.count && !phone.flying);
    assert(phone.session==123 && console.session==123 && console.duplicates);
    puts("PASS permanent notification stall: bounded read recovery, ordered fragmented delivery, late duplicate suppressed, session preserved");
}

static void notification_setup(void){
    static LrStream stream;ls_init(&stream,472,123);
    uint8_t heartbeat[500];size_t n=ls_prepare(&stream,heartbeat,sizeof(heartbeat),1);
    assert(n==LS_HEADER); /* Old pump sent this immediately after LRNA. */
    const uint8_t probe[12]={'L','R','N','A'};uint8_t snapshot[500];size_t length=0;
    bool awaiting_stream=true,has_probe=true;
    if(lr_notify_negotiated(awaiting_stream,has_probe)){memcpy(snapshot,probe,sizeof(probe));length=sizeof(probe);has_probe=false;}
    /* Several timer ticks before the Switch reads its last-event slot. */
    for(unsigned t=1;t<100;t++)if(lr_notify_negotiated(awaiting_stream,has_probe)){
        size_t hn=ls_prepare(&stream,heartbeat,sizeof(heartbeat),t);
        if(hn){memcpy(snapshot,heartbeat,hn);length=hn;ls_commit(&stream,heartbeat,hn,t);}
    }
    assert(length==sizeof(probe) && !memcmp(snapshot,probe,sizeof(probe)));
    assert(stream.tx_frames==0 && stream.ack_dirty);
    has_probe=true;assert(lr_notify_negotiated(awaiting_stream,has_probe)); /* Retry allowed. */
    awaiting_stream=false;assert(lr_notify_negotiated(awaiting_stream,false));
    n=ls_prepare(&stream,heartbeat,sizeof(heartbeat),100);assert(n==LS_HEADER);
    puts("PASS notification setup: probe survives snapshot API, no premature heartbeat, probe retries allowed, stream opens after peer confirmation");
}

int main(void){
    notification_setup();
    read_recovery();
    frame_cap_sweep();
    queued_batch_packing();
    ack_coalescing();
    /* Queue saturation never partially accepts a message. */
    static LrQueue q;LrMessage m;uint8_t p[3]={1,2,3};for(unsigned i=0;i<LR_QUEUE_DEPTH;i++)assert(lq_push(&q,p,3,i));assert(!lq_push(&q,p,3,99));
    for(unsigned i=0;i<LR_QUEUE_DEPTH;i++){uint64_t t;assert(lq_pop(&q,&m,&t) && t==i && m.size==3 && !memcmp(m.bytes,p,3));}assert(!lq_pop(&q,&m,NULL));
    static LrStream s;ls_init(&s,500,17);queue_message(&s,7);uint8_t a[500],b[500];size_t n=ls_prepare(&s,a,500,0);
    assert(n && !s.flying && s.count==1);s.received=9;assert(ls_prepare(&s,b,500,1)==n && lr_get16(b+6)==9);ls_commit(&s,b,n,1);assert(s.flying==1);
    LrStream other;ls_init(&other,500,18);assert(!ls_ingest(&other,b,n,receive,NULL,2));
    puts("PASS queue bounds, prepare/commit boundary, fresh ACK on retry, session isolation");
    independent_receive_credit();sender_window();retry_backoff();callback_stall();slow_progress();adversity(64,false);adversity(182,false);adversity(500,true);load(150);load(300);
}
