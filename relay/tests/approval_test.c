#include "relay_approval.h"
#include "relay_stream.h"
#include <assert.h>
#include <stdio.h>
static void connect_pair(LrApprovedSession *c,LrApprovedSession *s,unsigned seed){
    uint8_t nonce[16]={0},h[LP_HELLO],a[LP_HELLO];nonce[0]=seed;
    lp_reset(c);lp_reset(s);assert(lp_client_start(c,nonce,LP_INNER_MAX,h));
    assert(!c->active);assert(lp_server_accept(s,h,sizeof(h),a));assert(lp_client_confirm(c,a,sizeof(a)));
}
static void approval(void){
    LrApprovalGate g;lp_gate_start(&g,100);
    assert(!lp_gate_poll(&g,101,true,true,false)); /* held from starting scan */
    assert(!lp_gate_poll(&g,102,false,true,false));
    assert(!lp_gate_poll(&g,103,false,false,false));
    assert(lp_gate_poll(&g,104,true,true,false));
    lp_gate_start(&g,200);assert(!g.approved); /* every reconnect needs approval */
    assert(!lp_gate_poll(&g,201,false,false,false));
    assert(!lp_gate_poll(&g,202,true,true,true));assert(g.closed); /* reject wins */
    assert(!lp_gate_poll(&g,203,true,true,false));
    lp_gate_start(&g,300);assert(!lp_gate_poll(&g,301,false,false,false));
    assert(!lp_gate_poll(&g,60300,true,true,false));assert(g.closed); /* exact deadline */
}
static void framing(void){
    LrApprovedSession c={0},s={0};uint8_t nonce[16]={1},h[LP_HELLO],a[LP_HELLO],wire[500],out[500],payload[LP_INNER_MAX]={4};
    assert(!lp_wrap(&c,payload,1,wire,sizeof(wire)));assert(!lp_unwrap(&s,wire,100,out,sizeof(out)));
    assert(lp_client_start(&c,nonce,LP_INNER_MAX,h));
    for(size_t n=0;n<LP_HELLO;n++)assert(!lp_server_accept(&s,h,n,a));
    h[6]=1;assert(!lp_server_accept(&s,h,sizeof(h),a));h[6]=0;
    h[3]='4';assert(!lp_server_accept(&s,h,sizeof(h),a));h[3]='5';
    assert(lp_server_accept(&s,h,sizeof(h),a));a[8]^=1;
    assert(!lp_client_confirm(&c,a,sizeof(a)));a[8]^=1;assert(lp_client_confirm(&c,a,sizeof(a)));
    size_t n=lp_wrap(&c,payload,sizeof(payload),wire,sizeof(wire));assert(n==500);
    assert(!lp_unwrap(&c,wire,n,out,sizeof(out))); /* reflected direction */
    for(size_t i=0;i<=LP_OVERHEAD;i++)assert(!lp_unwrap(&s,wire,i,out,sizeof(out)));
    assert(!lp_unwrap(&s,wire,n,out,1));
    wire[4]^=1;assert(!lp_unwrap(&s,wire,n,out,sizeof(out)));wire[4]^=1;
    assert(lp_unwrap(&s,wire,n,out,sizeof(out))==sizeof(payload));assert(!memcmp(payload,out,sizeof(payload)));
    assert(!lp_unwrap(&s,wire,n,out,sizeof(out))); /* stale delivery */
    assert(lp_server_accept(&s,h,sizeof(h),a));assert(s.received==1); /* duplicate setup must not reset */
    h[8]^=2;assert(!lp_server_accept(&s,h,sizeof(h),a));h[8]^=2;
    connect_pair(&c,&s,2);assert(!lp_unwrap(&s,wire,n,out,sizeof(out)));
    n=lp_wrap_notification(&s,payload,120,wire,sizeof(wire));assert(n==148);
    assert(lp_unwrap(&c,wire,n,out,sizeof(out))==120);
    assert(!lp_wrap_notification(&c,payload,1,wire,sizeof(wire)));
    c.sent=UINT64_MAX;assert(!lp_wrap(&c,payload,1,wire,sizeof(wire)));
    /* Approval framing makes no tamper-resistance promise. */
    connect_pair(&c,&s,3);n=lp_wrap(&c,payload,120,wire,sizeof(wire));wire[LP_OVERHEAD]^=1;
    assert(lp_unwrap(&s,wire,n,out,sizeof(out))==120 && out[0]!=payload[0]);
}
static bool ordered(void *ctx,const uint8_t *p,size_t n){
    unsigned *next=ctx;assert(n==700 && lr_get32(p)==(*next)++);return true;
}
static void stream_loss(void){
    LrApprovedSession session[2];connect_pair(&session[0],&session[1],8);
    static LrStream stream[2];unsigned delivered[2]={0},generated[2]={0},sends=0;
    for(unsigned d=0;d<2;d++)ls_init(&stream[d],LP_INNER_MAX,991);
    for(uint64_t t=1;t<1000000 && (delivered[0]<50 || delivered[1]<50 || stream[0].flying || stream[1].flying);t++){
        for(unsigned d=0;d<2;d++){
            if(generated[d]<50 && stream[d].count<LR_QUEUE_DEPTH){uint8_t p[700]={0};lr_put32(p,generated[d]++);assert(ls_enqueue(&stream[d],p,sizeof(p)));}
            uint8_t plain[500],wire[500],decoded[500];size_t n=ls_prepare(&stream[d],plain,sizeof(plain),t);if(!n)continue;
            size_t wn=(d==0 || t%2)?lp_wrap(&session[d],plain,n,wire,sizeof(wire)):lp_wrap_notification(&session[d],plain,n,wire,sizeof(wire));assert(wn);
            ls_commit(&stream[d],plain,n,t);if(++sends%7==0)continue;
            size_t dn=lp_unwrap(&session[1-d],wire,wn,decoded,sizeof(decoded));assert(dn==n);
            assert(ls_ingest(&stream[1-d],decoded,dn,ordered,&delivered[1-d],t));
            assert(!lp_unwrap(&session[1-d],wire,wn,decoded,sizeof(decoded)));
        }
    }
    assert(delivered[0]==50 && delivered[1]==50 && !stream[0].flying && !stream[1].flying);
    puts("PASS approved fragmented stream: bidirectional loss, stale-frame rejection, mixed read/notification envelopes");
}
int main(void){approval();framing();stream_loss();puts("PASS local approval: fresh press, reject, timeout, reconnect reset, framing bounds and no security overclaim");}
