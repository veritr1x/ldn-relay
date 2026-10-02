/* MIT licence. See relay_stream.h for ownership and delivery guarantees. */
#include "relay_stream.h"
#include <string.h>
static uint16_t next(uint16_t v){return v==65535?1:(uint16_t)(v+1);}
static uint16_t crc(const uint8_t *p,size_t n){
    uint16_t c=65535;
    for(size_t i=0;i<n;i++)if(i!=14 && i!=15){c^=(uint16_t)p[i]<<8;for(unsigned j=0;j<8;j++)c=(uint16_t)((c<<1)^((c&32768)?0x1021:0));}
    return c;
}
void ls_init(LrStream *s,uint16_t limit,uint32_t session){
    memset(s,0,sizeof(*s));s->limit=limit<64?64:limit>LR_MAX_FRAME?LR_MAX_FRAME:limit;
    s->send_limit=s->limit;s->session=session;s->next=1;s->peer_credit=LS_WINDOW;s->send_window=LS_WINDOW;s->ready=true;s->ack_dirty=true;
}
bool ls_enqueue(LrStream *s,const void *p,size_t n){
    if(!p || !n || n>LR_MAX_MESSAGE || s->count==LR_QUEUE_DEPTH)return false;
    LrMessage *m=&s->queue[(s->head+s->count)%LR_QUEUE_DEPTH];m->size=(uint16_t)n;memcpy(m->bytes,p,n);s->count++;return true;
}
void ls_set_frame(LrStream *s,unsigned limit){s->send_limit=(uint16_t)(limit<64?64:limit>s->limit?s->limit:limit);}
void ls_set_window(LrStream *s,unsigned window){s->send_window=(uint8_t)(window<1?1:window>LS_WINDOW?LS_WINDOW:window);}
void ls_ready(LrStream *s,bool ready){if(s->ready!=ready){s->ready=ready;s->ack_dirty=true;}}
static void header(LrStream *s,uint8_t *p,size_t n){
    lr_put16(p+6,s->received);lr_put32(p+16,s->session);p[20]=s->ready?LS_WINDOW:0;
    p[21]=p[22]=p[23]=0;lr_put16(p+14,crc(p,n));
}
static uint64_t retry_timeout(const LrStream *s){
    uint64_t ms=s->ack_ms_max*2+50;
    if(ms<LS_RETRY_MS)ms=LS_RETRY_MS;
    if(ms>1000)ms=1000;
    return ms>s->retry_floor_ms?ms:s->retry_floor_ms;
}
size_t ls_prepare(LrStream *s,void *out,size_t capacity,uint64_t now){
    if(capacity<s->limit)return 0;
    uint8_t *p=out;size_t n=LS_HEADER;
    /* Preserve quick recovery on a fast link, but do not replay a whole window
       faster than this connection has demonstrated it can acknowledge it. */
    uint64_t retry_ms=retry_timeout(s);
    if(s->flying && !s->replay && now-s->progress>=retry_ms)s->replay=s->flying;
    if(s->replay){
        unsigned i=(s->first+s->flying-s->replay)%LS_WINDOW;
        n=s->flight[i].size;memcpy(p,s->flight[i].bytes,n);
    }else if(s->count && s->flying<s->send_window && s->flying<LS_WINDOW && s->flying<s->peer_credit){
        LrMessage *m=&s->queue[s->head];size_t part=m->size-s->offset;
        if(part>(size_t)s->send_limit-LS_HEADER)part=(size_t)s->send_limit-LS_HEADER;
        memset(p,0,LS_HEADER);p[0]='L';p[1]='R';p[2]=LS_VERSION;p[3]=1;
        lr_put16(p+4,s->next);lr_put16(p+8,(uint16_t)s->offset);lr_put16(p+10,m->size);lr_put16(p+12,(uint16_t)part);
        memcpy(p+LS_HEADER,m->bytes+s->offset,part);n+=part;
    }else{
        if(s->ack_dirty && s->ack_delay_ms && now<s->ack_due)return 0;
        if(!s->ack_dirty && now-s->last_sent<100)return 0;
        memset(p,0,LS_HEADER);p[0]='L';p[1]='R';p[2]=LS_VERSION;
    }
    header(s,p,n);return n;
}
void ls_commit(LrStream *s,const void *bytes,size_t n,uint64_t now){
    const uint8_t *p=bytes;if(n<LS_HEADER)return;
    if(p[3]){
        uint16_t seq=lr_get16(p+4);
        if(seq==s->next && s->count && s->flying<LS_WINDOW){
            LsFlight *f=&s->flight[(s->first+s->flying)%LS_WINDOW];memcpy(f->bytes,p,n);f->size=n;f->sent=now;f->retried=false;
            if(!s->flying)s->progress=now;
            s->flying++;s->next=next(s->next);s->offset+=lr_get16(p+12);
            if(s->offset==s->queue[s->head].size){s->head=(s->head+1)%LR_QUEUE_DEPTH;s->count--;s->offset=0;}
        }else{
            /* Back off once per committed replay round, not per prepare call
             * or fragment. Keep this floor until an unambiguous RTT arrives. */
            if(s->replay==s->flying){
                uint64_t ms=retry_timeout(s)*2;
                s->retry_floor_ms=ms>2000?2000:ms;
            }
            for(unsigned i=0;i<s->flying;i++){
                LsFlight *f=&s->flight[(s->first+i)%LS_WINDOW];
                if(lr_get16(f->bytes+4)==seq){f->sent=now;f->retried=true;s->retries++;break;}
            }
            if(s->replay)s->replay--;
            s->progress=now;
        }
    }
    s->last_sent=now;s->tx_frames++;
    if(lr_get16(p+6)==s->received && p[20]==(s->ready?LS_WINDOW:0))s->ack_dirty=false;
}
bool ls_ingest(LrStream *s,const void *bytes,size_t n,LrReceive receive,void *ctx,uint64_t now){
    const uint8_t *p=bytes;
    if(!p || n<LS_HEADER || n>s->limit || p[0]!='L'||p[1]!='R'||p[2]!=LS_VERSION || p[3]>1 ||
       p[20]>LS_WINDOW || p[21]||p[22]||p[23] || lr_get32(p+16)!=s->session || lr_get16(p+14)!=crc(p,n))goto malformed;
    uint16_t seq=lr_get16(p+4),ack=lr_get16(p+6),off=lr_get16(p+8),total=lr_get16(p+10),part=lr_get16(p+12);
    if(n!=LS_HEADER+(size_t)part || (p[3]?(!seq||!part||!total||total>LR_MAX_MESSAGE||off>total||part>total-off):(seq||off||total||part)))goto malformed;
    s->rx_frames++;
    if(ack==s->acked)s->peer_credit=p[20];
    for(unsigned i=0;i<s->flying;i++)if(lr_get16(s->flight[(s->first+i)%LS_WINDOW].bytes+4)==ack){
        for(unsigned j=0;j<=i;j++){
            LsFlight *f=&s->flight[(s->first+j)%LS_WINDOW];
            if(!f->retried){s->retry_floor_ms=0;uint64_t age=now-f->sent;s->ack_samples++;s->ack_ms_sum+=age;if(age>s->ack_ms_max)s->ack_ms_max=age;}
        }
        s->first=(s->first+i+1)%LS_WINDOW;s->flying-=i+1;s->acked=ack;s->peer_credit=p[20];s->replay=0;s->progress=now;break;
    }
    if(!p[3])return true;
    if(!s->ack_dirty)s->ack_due=now+s->ack_delay_ms;
    s->ack_dirty=true;
    if(seq!=next(s->received)){if(seq==s->received)s->duplicates++;else s->gaps++;return true;}
    if(off!=s->used || (off && total!=s->total))goto malformed;
    if(!off)s->total=total;
    memcpy(s->assembly+off,p+LS_HEADER,part);
    if(off+part==total){
        if(!receive || !receive(ctx,s->assembly,total)){s->blocked++;ls_ready(s,false);return true;}
        s->used=s->total=0;
    }else s->used=(uint16_t)(off+part);
    s->received=seq;return true;
malformed:s->malformed++;return false;
}
