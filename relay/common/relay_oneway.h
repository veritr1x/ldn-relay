/* MIT. Diagnostic one-way records. No timestamps are compared across devices. */
#ifndef RELAY_ONEWAY_H
#define RELAY_ONEWAY_H
#include "relay_codec.h"
#include <string.h>
#define OW_START 0x75
#define OW_DATA 0x76
#define OW_END 0x77
#define OW_REPORT 0x78
#define OW_PARAM 0x79
#define OW_PARAM_RESULT 0x7a
#define OW_SIZE 130
#define OW_MAX 4096
typedef struct {bool active;uint16_t id,rate,seconds;unsigned next,accepted,dropped,received,corrupt,duplicates;uint64_t start;uint8_t seen[OW_MAX/8];} LrOneWay;
static inline bool ow_start(LrOneWay *s,const uint8_t *p,size_t n,uint64_t now){
 if(n!=8 || p[0]!=OW_START || !lr_get16(p+1) || p[3]>2 || !lr_get16(p+4) || !lr_get16(p+6) || (uint32_t)lr_get16(p+4)*lr_get16(p+6)>OW_MAX)return false;
 memset(s,0,sizeof(*s));s->active=true;s->id=lr_get16(p+1);s->rate=lr_get16(p+4);s->seconds=lr_get16(p+6);s->start=now;return true;
}
static inline void ow_packet(uint8_t p[OW_SIZE],unsigned id,unsigned seq){
 p[0]=OW_DATA;lr_put16(p+1,id);lr_put32(p+3,seq);for(unsigned i=7;i<OW_SIZE;i++)p[i]=(uint8_t)(seq+i*17);
}
static inline void ow_receive(LrOneWay *s,const uint8_t *p,size_t n){
 if(!s->active || n<3 || lr_get16(p+1)!=s->id)return;
 if(n!=OW_SIZE || p[0]!=OW_DATA){s->corrupt++;return;}
 unsigned seq=lr_get32(p+3);if(seq>=s->rate*s->seconds){s->corrupt++;return;}
 for(unsigned i=7;i<n;i++)if(p[i]!=(uint8_t)(seq+i*17)){s->corrupt++;return;}
 if(s->seen[seq/8]&(1u<<(seq%8))){s->duplicates++;return;}
 s->seen[seq/8]|=(uint8_t)(1u<<(seq%8));s->received++;
}
static inline bool ow_report(LrOneWay *s,const uint8_t *p,size_t n,uint64_t now,uint8_t out[31]){
 if(!s->active || n!=15 || p[0]!=OW_END || lr_get16(p+1)!=s->id)return false;
 memcpy(out,p,15);out[0]=OW_REPORT;lr_put32(out+15,s->received);lr_put32(out+19,s->corrupt);lr_put32(out+23,s->duplicates);lr_put32(out+27,(uint32_t)(now-s->start));s->active=false;return true;
}
#endif
