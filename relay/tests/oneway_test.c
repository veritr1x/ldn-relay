#include "relay_oneway.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 LrOneWay s={0};uint8_t start[8]={OW_START,1,0,0,62,0,8,0},p[OW_SIZE],end[15]={OW_END,1,0},report[31];
 assert(ow_start(&s,start,8,1000));
 for(unsigned i=0;i<496;i++){ow_packet(p,1,i);ow_receive(&s,p,sizeof(p));}
 assert(s.received==496 && !s.corrupt && !s.duplicates);
 ow_receive(&s,p,sizeof(p));assert(s.duplicates==1);
 p[7]^=1;ow_receive(&s,p,sizeof(p));assert(s.corrupt==1);
 ow_packet(p,1,496);ow_receive(&s,p,sizeof(p));assert(s.corrupt==2);
 ow_packet(p,2,0);ow_receive(&s,p,sizeof(p));assert(s.received==496 && s.corrupt==2);
 lr_put32(end+3,496);lr_put32(end+7,496);assert(ow_report(&s,end,sizeof(end),9500,report));
 assert(lr_get32(report+15)==496 && lr_get32(report+27)==8500 && !s.active);
 assert(!ow_report(&s,end,sizeof(end),9501,report));
 start[3]=1;assert(ow_start(&s,start,8,10000));assert(!s.received && !s.duplicates && !s.corrupt);
 ow_packet(p,1,5);ow_receive(&s,p,sizeof(p));ow_packet(p,1,2);ow_receive(&s,p,sizeof(p));assert(s.received==2);
 start[3]=3;assert(!ow_start(&s,start,8,0));start[3]=0;start[4]=start[5]=255;assert(!ow_start(&s,start,8,0));
 LrOneWay tx,rx;uint8_t both[8]={OW_START,7,0,2,62,0,60,0};assert(ow_start(&tx,both,8,1));lr_put16(both+1,0x8007);assert(ow_start(&rx,both,8,1));
 for(unsigned i=0;i<3720;i++){ow_packet(p,7,i);ow_receive(&tx,p,OW_SIZE);ow_receive(&rx,p,OW_SIZE);ow_packet(p,0x8007,i);ow_receive(&rx,p,OW_SIZE);}
 assert(tx.received==3720 && rx.received==3720 && !tx.corrupt && !rx.corrupt);
 lr_put16(end+1,7);assert(ow_report(&tx,end,15,60001,report));assert(rx.active);lr_put16(end+1,0x8007);assert(ow_report(&rx,end,15,60002,report));
 puts("PASS duplex independent receiver state: 3720 records each, direction isolation, independent completion");
 puts("PASS one-way records: 496 exact packets, duplicates, corruption, bounds, epoch isolation, reordering, local elapsed time, one summary");
}
