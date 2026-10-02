#include "relay_host.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 uint8_t p[17+64+384]={3};lr_put64(p+1,1);lr_put16(p+9,22287);lr_put16(p+11,88);p[13]=2;p[14]=64;lr_put16(p+15,384);
 LrHostRequest r;assert(lr_host_parse(p,sizeof(p),&r));assert(r.advertisement==p+81 && r.scene==22287 && r.version==88);
 for(size_t n=0;n<sizeof(p);n++)assert(!lr_host_parse(p,n,&r));
 p[13]=9;assert(!lr_host_parse(p,sizeof(p),&r));p[13]=2;p[14]=65;assert(!lr_host_parse(p,sizeof(p),&r));p[14]=64;
 p[0]=2;assert(!lr_host_parse(p,sizeof(p),&r));p[0]=3;lr_put16(p+11,65535);assert(!lr_host_parse(p,sizeof(p),&r));
 puts("PASS native host request bounds, lengths, profile fields, key range");
}
