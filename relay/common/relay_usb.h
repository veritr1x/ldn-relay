/* MIT. Fixed USB pages preserve transfer boundaries; TCP uses the same pages. */
#ifndef RELAY_USB_H
#define RELAY_USB_H
#include "relay_protocol.h"
#include <string.h>
#define LU_PAGE 4096
#define LU_PORT 42387
static inline bool lu_pack(uint8_t *page,const void *message,size_t n){
    if(!message || n<3 || n>LR_MAX_MESSAGE)return false;
    memset(page,0,LU_PAGE);memcpy(page,"LDU1",4);lr_put16(page+4,(uint16_t)n);memcpy(page+8,message,n);return true;
}
static inline bool lu_unpack(const uint8_t *page,size_t size,const uint8_t **message,size_t *n){
    if(size!=LU_PAGE || memcmp(page,"LDU1",4) || page[6] || page[7])return false;
    size_t length=lr_get16(page+4);if(length<3 || length>LR_MAX_MESSAGE)return false;
    *message=page+8;*n=length;return true;
}
#endif
