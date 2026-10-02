/* MIT. Endpoint header compression for an ordered, reliable message stream.
 * Encode using a COPY of the state; commit only after enqueue succeeds.
 * Decode using a COPY; commit only after the message is accepted downstream.
 * Each direction has an independent cache, cleared on transport reset.
 * A full record seeds the slot; endpoint changes always use a full record.
 * Negotiation and opcode assignment are the caller's responsibility. */
#ifndef RELAY_COMPACT_H
#define RELAY_COMPACT_H
#include "relay_protocol.h"
#include <string.h>
typedef struct { bool valid[RELAY_MAX_SOCKETS];uint8_t endpoint[RELAY_MAX_SOCKETS][6]; } LrCompact;
static inline size_t lr_compact_encode(LrCompact *c,const uint8_t *p,size_t n,
                                      uint8_t full,uint8_t compact,uint8_t *out,size_t cap) {
    if(!p || n<10 || n>10+RELAY_MAX_UDP || p[0]!=full || p[3]>=RELAY_MAX_SOCKETS)return 0;
    unsigned slot=p[3];
    bool shortHeader=c->valid[slot] && !memcmp(c->endpoint[slot],p+4,6);
    size_t size=shortHeader?n-6:n;if(cap<size)return 0;
    if(shortHeader){out[0]=compact;out[1]=p[1];out[2]=p[2];out[3]=(uint8_t)slot;memcpy(out+4,p+10,n-10);}
    else {memcpy(out,p,n);memcpy(c->endpoint[slot],p+4,6);c->valid[slot]=true;}
    return size;
}
static inline size_t lr_compact_decode(LrCompact *c,const uint8_t *p,size_t n,
                                      uint8_t full,uint8_t compact,uint8_t *out,size_t cap) {
    if(!p || !n)return 0;
    if(p[0]==full){
        if(n<10 || n>10+RELAY_MAX_UDP || p[3]>=RELAY_MAX_SOCKETS || cap<n)return 0;
        memcpy(out,p,n);memcpy(c->endpoint[p[3]],p+4,6);c->valid[p[3]]=true;return n;
    }
    if(p[0]!=compact || n<4 || n>4+RELAY_MAX_UDP || p[3]>=RELAY_MAX_SOCKETS || !c->valid[p[3]] || cap<n+6)return 0;
    out[0]=full;out[1]=p[1];out[2]=p[2];out[3]=p[3];memcpy(out+4,c->endpoint[p[3]],6);memcpy(out+10,p+4,n-4);return n+6;
}
#endif
