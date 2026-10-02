#include "relay_auth.h"
#include "relay_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void pair(LrAuth *c,LrAuth *s,const uint8_t key[32],unsigned seed){
    uint8_t cn[32]={0},sn[32]={0},h[LA_HELLO],a[LA_CHALLENGE],f[LA_FINISH],k[LA_FINISH];cn[0]=seed;sn[0]=seed+1;
    la_client_start(c,key,cn,h);assert(la_server_start(s,key,sn,h,sizeof(h),a));
    assert(la_client_finish(c,key,a,sizeof(a),f));assert(la_server_finish(s,key,f,sizeof(f),k));assert(la_client_confirm(c,key,k,sizeof(k)));
}
static bool ordered(void *ctx,const uint8_t *p,size_t n){
    unsigned *next=ctx;assert(n==700 && lr_get32(p)==(*next)++);return true;
}
static void authenticated_loss(void){
    uint8_t key[32]={3};LrAuth auth[2];pair(&auth[0],&auth[1],key,8);
    static LrStream stream[2];unsigned delivered[2]={0},generated[2]={0},sends=0;
    for(unsigned d=0;d<2;d++)ls_init(&stream[d],LA_INNER_MAX,991);
    for(uint64_t t=1;t<1000000 && (delivered[0]<50 || delivered[1]<50 || stream[0].flying || stream[1].flying);t++){
        for(unsigned d=0;d<2;d++){
            if(generated[d]<50 && stream[d].count<LR_QUEUE_DEPTH){uint8_t p[700]={0};lr_put32(p,generated[d]++);assert(ls_enqueue(&stream[d],p,sizeof(p)));}
            uint8_t plain[500],wire[500],decoded[500];size_t n=ls_prepare(&stream[d],plain,sizeof(plain),t);if(!n)continue;
            size_t wn=(t%2)?la_wrap(&auth[d],plain,n,wire,sizeof(wire)):la_wrap_notification(&auth[d],plain,n,wire,sizeof(wire));assert(wn);
            ls_commit(&stream[d],plain,n,t);if(++sends%7==0)continue;
            size_t dn=la_unwrap(&auth[1-d],wire,wn,decoded,sizeof(decoded));assert(dn==n);
            assert(ls_ingest(&stream[1-d],decoded,dn,ordered,&delivered[1-d],t));
            assert(!la_unwrap(&auth[1-d],wire,wn,decoded,sizeof(decoded)));
        }
    }
    assert(delivered[0]==50 && delivered[1]==50 && !stream[0].flying && !stream[1].flying);
    puts("PASS authenticated fragmented stream: bidirectional loss, replay rejection, fresh MAC on retry, mixed read/notification envelopes");
}
int main(void){
    authenticated_loss();
    /* RFC 4231 test case 1. */
    uint8_t key[32]={0},out[500],decoded[500],tag[32];memset(key,0x0b,20);
    const uint8_t expected[32]={0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b,0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7,0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7};
    la_hmac(key,20,"Hi There",8,tag);assert(!memcmp(tag,expected,32));
    LrAuth c={0},s={0};uint8_t wrong[32]={9},cn[32]={1},sn[32]={2},h[LA_HELLO],a[LA_CHALLENGE],f[LA_FINISH],k[LA_FINISH];
    la_client_start(&c,key,cn,h);assert(!la_server_start(&s,wrong,sn,h,sizeof(h),a));
    for(unsigned i=0;i<sizeof(h);i++){h[i]^=1;assert(!la_server_start(&s,key,sn,h,sizeof(h),a));h[i]^=1;}
    assert(la_server_start(&s,key,sn,h,sizeof(h),a));
    for(unsigned i=0;i<sizeof(a);i++){a[i]^=1;assert(!la_client_finish(&c,key,a,sizeof(a),f));a[i]^=1;}
    assert(la_client_finish(&c,key,a,sizeof(a),f));f[4]^=1;assert(!la_server_finish(&s,key,f,sizeof(f),k));f[4]^=1;
    assert(la_server_finish(&s,key,f,sizeof(f),k));assert(la_client_confirm(&c,key,k,sizeof(k)));
    uint8_t payload[LA_INNER_MAX];memset(payload,0xaa,sizeof(payload));
    size_t n=la_wrap(&c,payload,sizeof(payload),out,sizeof(out));assert(n==500);
    for(unsigned i=0;i<n;i++){out[i]^=1;assert(!la_unwrap(&s,out,n,decoded,sizeof(decoded)));out[i]^=1;assert(!s.received);}
    assert(!la_unwrap(&c,out,n,decoded,sizeof(decoded))); /* reflection */
    assert(la_unwrap(&s,out,n,decoded,sizeof(decoded))==sizeof(payload));assert(!memcmp(decoded,payload,sizeof(payload)));
    assert(!la_unwrap(&s,out,n,decoded,sizeof(decoded))); /* replay */
    assert(la_server_finish(&s,key,f,sizeof(f),k));assert(s.received==1); /* duplicate finish cannot reset replay guard */
    pair(&c,&s,key,4);assert(!la_unwrap(&s,out,n,decoded,sizeof(decoded))); /* prior session */
    for(unsigned i=0;i<10;i++)assert(la_wrap(&s,payload,120,out,sizeof(out)));
    assert(la_unwrap(&c,out,148,decoded,sizeof(decoded))==120); /* gaps allowed */
    for(unsigned i=0;i<LA_OVERHEAD;i++)assert(!la_unwrap(&c,out,i,decoded,sizeof(decoded)));
    size_t notify_n=la_wrap_notification(&s,payload,120,out,sizeof(out));assert(notify_n==148 && !memcmp(out,"LRN4",4));
    out[2]='D';assert(!la_unwrap(&c,out,notify_n,decoded,sizeof(decoded)));out[2]='N';
    assert(la_unwrap(&c,out,notify_n,decoded,sizeof(decoded))==120);
    c.sent=UINT64_MAX;assert(!la_wrap(&c,payload,1,out,sizeof(out)));
    la_wipe(&c,sizeof(c));assert(!c.active && !c.sent);
    puts("PASS pairing: RFC HMAC vector, wrong key, all-byte tampering, mutual proof, replay, reflection, old session, bounded frames, counter exhaustion");
}
