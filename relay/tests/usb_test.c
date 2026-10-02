#include "relay_usb.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    uint8_t message[LR_MAX_MESSAGE], page[LU_PAGE];
    for(size_t i=0;i<sizeof(message);i++)message[i]=(uint8_t)i;
    const size_t sizes[]={3,120,1027,LR_MAX_MESSAGE};
    const uint8_t *out;size_t n;
    for(size_t i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++){
        assert(lu_pack(page,message,sizes[i]));
        assert(lu_unpack(page,sizeof(page),&out,&n));
        assert(n==sizes[i] && !memcmp(out,message,n));
        for(size_t cut=0;cut<LU_PAGE;cut++)assert(!lu_unpack(page,cut,&out,&n));
    }
    assert(!lu_pack(page,message,2));assert(!lu_pack(page,message,LR_MAX_MESSAGE+1));
    page[0]^=1;assert(!lu_unpack(page,LU_PAGE,&out,&n));page[0]^=1;
    page[6]=1;assert(!lu_unpack(page,LU_PAGE,&out,&n));page[6]=0;
    page[7]=1;assert(!lu_unpack(page,LU_PAGE,&out,&n));page[7]=0;
    lr_put16(page+4,LR_MAX_MESSAGE+1);assert(!lu_unpack(page,LU_PAGE,&out,&n));
    lr_put16(page+4,2);assert(!lu_unpack(page,LU_PAGE,&out,&n));
    puts("USB page bounds and framing tests passed");return 0;
}
