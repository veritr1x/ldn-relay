#include "relay_compact.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    LrCompact tx={0},rx={0};uint8_t p[1410]={RL_SEND,0,0,0,192,168,1,4,0x10,0x20},wire[1410],out[1410];
    for(size_t n=10;n<=sizeof(p);n++){
        for(unsigned slot=0;slot<4;slot++){
            p[3]=(uint8_t)slot;p[4]=(uint8_t)n;
            size_t a=lr_compact_encode(&tx,p,n,RL_SEND,0x70,wire,sizeof(wire));assert(a==n);
            assert(lr_compact_decode(&rx,wire,a,RL_SEND,0x70,out,sizeof(out))==n && !memcmp(p,out,n));
            a=lr_compact_encode(&tx,p,n,RL_SEND,0x70,wire,sizeof(wire));assert(a==n-6);
            assert(lr_compact_decode(&rx,wire,a,RL_SEND,0x70,out,sizeof(out))==n && !memcmp(p,out,n));
        }
    }
    memset(&rx,0,sizeof(rx));assert(!lr_compact_decode(&rx,wire,3,RL_SEND,0x70,out,sizeof(out)));
    LrCompact before=tx;assert(!lr_compact_encode(&tx,p,sizeof(p),RL_SEND,0x70,wire,1));assert(!memcmp(&before,&tx,sizeof(tx)));
    p[1]=1;assert(lr_compact_encode(&tx,p,10,RL_SEND,0x70,wire,sizeof(wire))==4);assert(wire[1]==1);
    p[3]=4;assert(!lr_compact_encode(&tx,p,10,RL_SEND,0x70,wire,sizeof(wire)));
    /* Failed enqueue must not seed a peer's cache; retry sends a full header. */
    memset(&tx,0,sizeof(tx));memset(&rx,0,sizeof(rx));p[3]=0;p[1]=0;
    LrCompact candidate=tx;
    assert(lr_compact_encode(&candidate,p,130,RL_SEND,0x70,wire,sizeof(wire))==130);
    assert(!tx.valid[0]); /* discard candidate on backpressure */
    candidate=tx;assert(lr_compact_encode(&candidate,p,130,RL_SEND,0x70,wire,sizeof(wire))==130);tx=candidate;
    candidate=rx;assert(lr_compact_decode(&candidate,wire,130,RL_SEND,0x70,out,sizeof(out))==130);
    assert(!rx.valid[0]); /* downstream refused; retry without committing */
    assert(lr_compact_decode(&rx,wire,130,RL_SEND,0x70,out,sizeof(out))==130);
    assert(lr_compact_encode(&tx,p,130,RL_SEND,0x70,wire,sizeof(wire))==124);
    assert(lr_compact_decode(&rx,wire,124,RL_SEND,0x70,out,sizeof(out))==130 && !memcmp(p,out,130));
    puts("PASS transactional sender/receiver cache backpressure");
    puts("PASS compact endpoint roundtrip: all UDP lengths, slots, endpoint changes, request IDs, missing cache, bounded output");
}
