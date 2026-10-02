/* MIT. Local user approval and non-secret session framing. NOT authentication. */
#ifndef RELAY_APPROVAL_H
#define RELAY_APPROVAL_H
#include "relay_codec.h"
#include <string.h>
#define LP_HELLO 24
#define LP_OVERHEAD 28
#define LP_INNER_MAX (LR_MAX_FRAME-LP_OVERHEAD)
typedef struct {
    uint8_t nonce[16];
    uint16_t limit;
    uint64_t sent,received;
    bool active,client;
} LrApprovedSession;
typedef struct {uint64_t deadline;bool released,approved,closed;} LrApprovalGate;
static inline void lp_gate_start(LrApprovalGate *g,uint64_t now){
    *g=(LrApprovalGate){.deadline=now+60000};
}
static inline bool lp_gate_poll(LrApprovalGate *g,uint64_t now,bool down,bool held,bool cancel){
    if(g->closed)return g->approved;
    if(cancel || now>=g->deadline){g->closed=true;return false;}
    if(!held)g->released=true;
    if(g->released && down){g->closed=g->approved=true;}
    return g->approved;
}
static inline void lp_reset(LrApprovedSession *s){memset(s,0,sizeof(*s));}
static inline bool lp_hello_valid(const uint8_t *p,size_t n,const char *magic){
    if(!p || n!=LP_HELLO || memcmp(p,magic,4) || p[6] || p[7] ||
       lr_get16(p+4)<64 || lr_get16(p+4)>LP_INNER_MAX)return false;
    unsigned any=0;for(unsigned i=8;i<LP_HELLO;i++)any|=p[i];return any!=0;
}
static inline void lp_hello(const LrApprovedSession *s,const char *magic,uint8_t out[LP_HELLO]){
    memset(out,0,LP_HELLO);memcpy(out,magic,4);lr_put16(out+4,s->limit);memcpy(out+8,s->nonce,16);
}
static inline bool lp_client_start(LrApprovedSession *s,const uint8_t nonce[16],uint16_t limit,uint8_t out[LP_HELLO]){
    lp_reset(s);s->client=true;s->limit=limit;memcpy(s->nonce,nonce,16);
    lp_hello(s,"LRO5",out);return lp_hello_valid(out,LP_HELLO,"LRO5");
}
static inline bool lp_server_accept(LrApprovedSession *s,const void *data,size_t n,uint8_t out[LP_HELLO]){
    const uint8_t *p=data;if(!lp_hello_valid(p,n,"LRO5"))return false;
    if(s->active){if(s->client || s->limit!=lr_get16(p+4) || memcmp(s->nonce,p+8,16))return false;}
    else{lp_reset(s);s->active=true;s->limit=lr_get16(p+4);memcpy(s->nonce,p+8,16);}
    lp_hello(s,"LOA5",out);return true;
}
static inline bool lp_client_confirm(LrApprovedSession *s,const void *data,size_t n){
    const uint8_t *p=data;if(!s->client || s->active || !lp_hello_valid(p,n,"LOA5") ||
       s->limit!=lr_get16(p+4) || memcmp(s->nonce,p+8,16))return false;
    s->active=true;return true;
}
static inline size_t lp_wrap_kind(LrApprovedSession *s,const void *p,size_t n,void *output,size_t cap,bool notification){
    if(!s->active || !p || !output || !n || n>s->limit || cap<n+LP_OVERHEAD || s->sent==UINT64_MAX || (notification && s->client))return 0;
    uint8_t *out=output;memcpy(out,s->client?"LRC5":notification?"LRN5":"LRD5",4);
    memcpy(out+4,s->nonce,16);lr_put64(out+20,++s->sent);memcpy(out+LP_OVERHEAD,p,n);return n+LP_OVERHEAD;
}
static inline size_t lp_wrap(LrApprovedSession *s,const void *p,size_t n,void *out,size_t cap){return lp_wrap_kind(s,p,n,out,cap,false);}
static inline size_t lp_wrap_notification(LrApprovedSession *s,const void *p,size_t n,void *out,size_t cap){return lp_wrap_kind(s,p,n,out,cap,true);}
static inline size_t lp_unwrap(LrApprovedSession *s,const void *data,size_t n,void *out,size_t cap){
    const uint8_t *p=data;
    if(!s->active || !p || !out || n<=LP_OVERHEAD || n>(size_t)LP_OVERHEAD+s->limit || cap<n-LP_OVERHEAD)return 0;
    if(s->client?(memcmp(p,"LRD5",4) && memcmp(p,"LRN5",4)):memcmp(p,"LRC5",4))return 0;
    uint64_t seq=lr_get64(p+20);
    if(memcmp(p+4,s->nonce,16) || !seq || seq<=s->received)return 0;
    /* Detect delayed traffic only. Nonces/counters are public and forgeable. */
    memcpy(out,p+LP_OVERHEAD,n-LP_OVERHEAD);s->received=seq;return n-LP_OVERHEAD;
}
#endif
