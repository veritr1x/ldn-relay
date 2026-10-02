#include "relay_codec.h"
#include <string.h>

uint16_t lr_get16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
uint32_t lr_get32(const uint8_t *p) { return lr_get16(p) | ((uint32_t)lr_get16(p+2) << 16); }
uint64_t lr_get64(const uint8_t *p) { return lr_get32(p) | ((uint64_t)lr_get32(p+4) << 32); }
void lr_put16(uint8_t *p, uint16_t x) { p[0]=(uint8_t)x; p[1]=(uint8_t)(x>>8); }
void lr_put32(uint8_t *p, uint32_t x) { lr_put16(p,(uint16_t)x); lr_put16(p+2,(uint16_t)(x>>16)); }
void lr_put64(uint8_t *p, uint64_t x) { lr_put32(p,(uint32_t)x); lr_put32(p+4,(uint32_t)(x>>32)); }
static uint16_t next_seq(uint16_t n) { return n==UINT16_MAX ? 1 : (uint16_t)(n+1); }
static uint16_t crc_add(uint16_t crc, uint8_t b) {
    crc ^= (uint16_t)b << 8;
    for (unsigned i=0;i<8;i++) crc=(uint16_t)((crc<<1)^((crc&0x8000)?0x1021:0));
    return crc;
}
static uint16_t frame_crc(const uint8_t *p, size_t n) {
    uint16_t crc=0xffff;
    for (size_t i=0;i<n;i++) if (i!=14 && i!=15) crc=crc_add(crc,p[i]);
    return crc;
}
void lr_init(LrCodec *c, uint16_t limit) {
    memset(c,0,sizeof(*c));
    c->frame_limit=limit<20?20:limit>LR_MAX_FRAME?LR_MAX_FRAME:limit;
    c->tx_seq=1;
}
bool lr_enqueue(LrCodec *c, const void *bytes, size_t size) {
    if (!bytes || !size || size>LR_MAX_MESSAGE || c->count==LR_QUEUE_DEPTH) return false;
    LrMessage *m=&c->queue[(c->head+c->count)%LR_QUEUE_DEPTH];
    memcpy(m->bytes,bytes,size); m->size=(uint16_t)size; c->count++;
    return true;
}
size_t lr_prepare(const LrCodec *c, void *out, size_t capacity) {
    if (capacity<c->frame_limit) return 0;
    uint8_t *p=out;
    memset(p,0,LR_HEADER_SIZE); p[0]='L';p[1]='R';p[2]=LR_VERSION;
    lr_put16(p+6,c->rx_ack);
    size_t part=0;
    if (c->count) {
        const LrMessage *m=&c->queue[c->head];
        part=m->size-c->tx_offset;
        if (part>(size_t)c->frame_limit-LR_HEADER_SIZE) part=c->frame_limit-LR_HEADER_SIZE;
        p[3]=1;lr_put16(p+4,c->tx_seq);lr_put16(p+8,c->tx_offset);
        lr_put16(p+10,m->size);lr_put16(p+12,(uint16_t)part);
        memcpy(p+LR_HEADER_SIZE,m->bytes+c->tx_offset,part);
    }
    lr_put16(p+14,frame_crc(p,LR_HEADER_SIZE+part));
    return LR_HEADER_SIZE+part;
}
void lr_commit(LrCodec *c,const void *frame,size_t size) {
    const uint8_t *p=frame;
    if(size>=LR_HEADER_SIZE && p[3] && c->count && lr_get16(p+4)==c->tx_seq && lr_get16(p+8)==c->tx_offset)
        c->tx_part=lr_get16(p+12);
}
size_t lr_frame(LrCodec *c,void *out,size_t capacity) {
    size_t n=lr_prepare(c,out,capacity);if(n)lr_commit(c,out,n);return n;
}
bool lr_ingest(LrCodec *c, const void *frame, size_t size, LrReceive receive, void *ctx) {
    const uint8_t *p=frame;
    if (!p || size<LR_HEADER_SIZE || size>c->frame_limit || size>LR_MAX_FRAME) goto malformed;
    if (p[0]!='L'||p[1]!='R'||p[2]!=LR_VERSION||p[3]>1) goto malformed;
    uint16_t seq=lr_get16(p+4), ack=lr_get16(p+6), off=lr_get16(p+8);
    uint16_t total=lr_get16(p+10), part=lr_get16(p+12);
    if (size!=LR_HEADER_SIZE+(size_t)part || lr_get16(p+14)!=frame_crc(p,size)) goto malformed;
    if (p[3]) {
        if (!seq || !part || !total || total>LR_MAX_MESSAGE || off>total || part>total-off) goto malformed;
    } else if (seq||off||total||part) goto malformed;
    /* A valid ACK retires only an actually emitted fragment. */
    if (c->count && c->tx_part && ack==c->tx_seq) {
        c->tx_offset=(uint16_t)(c->tx_offset+c->tx_part); c->tx_part=0;c->tx_seq=next_seq(c->tx_seq);
        if (c->tx_offset==c->queue[c->head].size) {
            c->head=(c->head+1)%LR_QUEUE_DEPTH;c->count--;c->tx_offset=0;c->sent_messages++;
        }
    }
    if (!p[3]) return true;
    if (seq==c->rx_ack) { c->duplicate_frames++;return true; }
    if (seq!=next_seq(c->rx_ack)) return false;
    if (off!=c->rx_used || (off && total!=c->rx_size)) goto malformed;
    if (!off) c->rx_size=total;
    memcpy(c->rx_bytes+off,p+LR_HEADER_SIZE,part);
    if (off+part==total) {
        if (!receive || !receive(ctx,c->rx_bytes,total)) return false;
        c->received_messages++;c->rx_used=0;c->rx_size=0;
    } else c->rx_used=(uint16_t)(off+part);
    c->rx_ack=seq;
    return true;
malformed:
    c->malformed_frames++;return false;
}
