/* MIT. PSK authentication using platform HMAC-SHA256, not payload encryption. */
#ifndef RELAY_AUTH_H
#define RELAY_AUTH_H
#include "relay_codec.h"
#define LA_KEY 32
#define LA_HELLO 76
#define LA_CHALLENGE 108
#define LA_FINISH 36
#define LA_OVERHEAD 28
#define LA_INNER_MAX (LR_MAX_FRAME-LA_OVERHEAD)
typedef struct {
    uint8_t hello[LA_HELLO],challenge[LA_CHALLENGE];
    uint8_t tx_key[32],rx_key[32];
    uint64_t sent,received;
    bool pending,active;
} LrAuth;
void la_wipe(void *,size_t);
void la_hmac(const void *key,size_t key_size,const void *p,size_t n,uint8_t out[32]);
void la_client_start(LrAuth *,const uint8_t key[32],const uint8_t random[32],uint8_t out[LA_HELLO]);
bool la_server_start(LrAuth *,const uint8_t key[32],const uint8_t random[32],const void *,size_t,uint8_t out[LA_CHALLENGE]);
bool la_client_finish(LrAuth *,const uint8_t key[32],const void *,size_t,uint8_t out[LA_FINISH]);
bool la_server_finish(LrAuth *,const uint8_t key[32],const void *,size_t,uint8_t out[LA_FINISH]);
bool la_client_confirm(LrAuth *,const uint8_t key[32],const void *,size_t);
size_t la_wrap_notification(LrAuth *,const void *,size_t,void *,size_t);
size_t la_wrap(LrAuth *,const void *,size_t,void *,size_t);
size_t la_unwrap(LrAuth *,const void *,size_t,void *,size_t);
#endif
