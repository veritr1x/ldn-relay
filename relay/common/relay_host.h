/* MIT. Generic native LDN hosting request; no game profile or key baked in. */
#ifndef RELAY_HOST_H
#define RELAY_HOST_H
#include "relay_codec.h"
#include <string.h>
typedef struct {
    uint8_t protocol, max_nodes, key_size;
    uint64_t communication_id;
    uint16_t scene, version, advertisement_size;
    const uint8_t *key, *advertisement;
} LrHostRequest;
/* proto:u8, comm:u64le, scene:u16le, version:u16le, max_nodes:u8,
 * key_size:u8, ad_size:u16le, key[16..64], ad[0..384]. Channel is native-auto. */
static inline bool lr_host_parse(const uint8_t *p,size_t n,LrHostRequest *out){
    if(!p || !out || n<17 || (p[0]!=1 && p[0]!=3) || !lr_get64(p+1) ||
       lr_get16(p+11)>32767 || p[13]<2 || p[13]>8 || p[14]<16 || p[14]>64 ||
       lr_get16(p+15)>384 || n!=17u+p[14]+lr_get16(p+15))return false;
    *out=(LrHostRequest){.protocol=p[0],.communication_id=lr_get64(p+1),.scene=lr_get16(p+9),
        .version=lr_get16(p+11),.max_nodes=p[13],.key_size=p[14],.advertisement_size=lr_get16(p+15),
        .key=p+17,.advertisement=p+17+p[14]};return true;
}
#endif
