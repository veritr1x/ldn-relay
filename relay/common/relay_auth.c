#include "relay_auth.h"
#include <string.h>
#ifdef __SWITCH__
#include <switch.h>
#elif defined(__APPLE__)
#include <CommonCrypto/CommonHMAC.h>
#else
#include <openssl/hmac.h>
#endif
void la_wipe(void *v,size_t n){volatile unsigned char *p=v;while(n--)*p++=0;}
void la_hmac(const void *key,size_t key_size,const void *p,size_t n,uint8_t out[32]){
#ifdef __SWITCH__
    hmacSha256CalculateMac(out,key,key_size,p,n);
#elif defined(__APPLE__)
    CCHmac(kCCHmacAlgSHA256,key,key_size,p,n,out);
#else
    unsigned length=32;HMAC(EVP_sha256(),key,(int)key_size,p,n,out,&length);
#endif
}
static bool equal(const uint8_t *a,const uint8_t *b,size_t n){volatile unsigned difference=0;for(size_t i=0;i<n;i++)difference|=a[i]^b[i];return difference==0;}
static bool valid(const uint8_t key[32],const uint8_t *p,size_t n){uint8_t tag[32];la_hmac(key,32,p,n-32,tag);bool ok=equal(tag,p+n-32,32);la_wipe(tag,32);return ok;}
/* Transcript is the entire authenticated challenge prefix: protocol/version,
   client nonce and server nonce. Labels separate keys from handshake proofs. */
static void proof(const LrAuth *s,const uint8_t key[32],const char label[4],uint8_t out[32]){
    uint8_t input[80];memcpy(input,label,4);memcpy(input+4,s->challenge,76);la_hmac(key,32,input,sizeof(input),out);la_wipe(input,sizeof(input));
}
static void keys(LrAuth *s,const uint8_t key[32],bool server){
    proof(s,key,server?"S2C4":"C2S4",s->tx_key);proof(s,key,server?"C2S4":"S2C4",s->rx_key);
}
void la_client_start(LrAuth *s,const uint8_t key[32],const uint8_t random[32],uint8_t out[LA_HELLO]){
    la_wipe(s,sizeof(*s));memcpy(s->hello,"LRH4",4);lr_put16(s->hello+4,LA_INNER_MAX);
    memcpy(s->hello+12,random,32);la_hmac(key,32,s->hello,44,s->hello+44);s->pending=true;memcpy(out,s->hello,LA_HELLO);
}
bool la_server_start(LrAuth *s,const uint8_t key[32],const uint8_t random[32],const void *v,size_t n,uint8_t out[LA_CHALLENGE]){
    const uint8_t *p=v;
    if(n!=LA_HELLO || memcmp(p,"LRH4",4) || lr_get16(p+4)!=LA_INNER_MAX || memcmp(p+6,"\0\0\0\0\0\0",6) || !valid(key,p,n))return false;
    la_wipe(s,sizeof(*s));memcpy(s->hello,p,n);memcpy(s->challenge,p,44);memcpy(s->challenge,"LRA4",4);memcpy(s->challenge+44,random,32);
    la_hmac(key,32,s->challenge,76,s->challenge+76);s->pending=true;memcpy(out,s->challenge,LA_CHALLENGE);return true;
}
bool la_client_finish(LrAuth *s,const uint8_t key[32],const void *v,size_t n,uint8_t out[LA_FINISH]){
    const uint8_t *p=v;
    if(!s->pending || s->active || n!=LA_CHALLENGE || memcmp(p,"LRA4",4) || memcmp(p+4,s->hello+4,40) || !valid(key,p,n))return false;
    memcpy(s->challenge,p,n);keys(s,key,false);memcpy(out,"LRF4",4);proof(s,key,"LRF4",out+4);return true;
}
bool la_server_finish(LrAuth *s,const uint8_t key[32],const void *v,size_t n,uint8_t out[LA_FINISH]){
    const uint8_t *p=v;uint8_t tag[32];
    if(!s->pending || n!=LA_FINISH || memcmp(p,"LRF4",4))return false;
    proof(s,key,"LRF4",tag);bool ok=equal(tag,p+4,32);la_wipe(tag,32);if(!ok)return false;
    if(!s->active){keys(s,key,true);s->active=true;} /* Idempotent confirmation; counters never reset. */
    memcpy(out,"LRK4",4);proof(s,key,"LRK4",out+4);return true;
}
bool la_client_confirm(LrAuth *s,const uint8_t key[32],const void *v,size_t n){
    const uint8_t *p=v;uint8_t tag[32];
    if(!s->pending || s->active || n!=LA_FINISH || memcmp(p,"LRK4",4))return false;
    proof(s,key,"LRK4",tag);bool ok=equal(tag,p+4,32);la_wipe(tag,32);if(ok)s->active=true;return ok;
}
static size_t wrap(LrAuth *s,const void *p,size_t n,void *v,size_t cap,bool notify){
    if(!s->active || !n || n>LA_INNER_MAX || cap<n+LA_OVERHEAD || s->sent==UINT64_MAX)return 0;
    uint8_t *out=v,tag[32];memcpy(out,notify?"LRN4":"LRD4",4);lr_put64(out+4,++s->sent);memcpy(out+12,p,n);
    la_hmac(s->tx_key,32,out,n+12,tag);memcpy(out+12+n,tag,16);la_wipe(tag,32);return n+LA_OVERHEAD;
}
size_t la_wrap(LrAuth *s,const void *p,size_t n,void *v,size_t cap){return wrap(s,p,n,v,cap,false);}
size_t la_wrap_notification(LrAuth *s,const void *p,size_t n,void *v,size_t cap){return wrap(s,p,n,v,cap,true);}
size_t la_unwrap(LrAuth *s,const void *v,size_t n,void *out,size_t cap){
    const uint8_t *p=v;
    if(!s->active || n<=LA_OVERHEAD || n>LR_MAX_FRAME || cap<n-LA_OVERHEAD || (memcmp(p,"LRD4",4) && memcmp(p,"LRN4",4)))return 0;
    uint64_t counter=lr_get64(p+4);if(counter<=s->received)return 0;
    uint8_t tag[32];la_hmac(s->rx_key,32,p,n-16,tag);bool ok=equal(tag,p+n-16,16);la_wipe(tag,32);if(!ok)return 0;
    memcpy(out,p+12,n-LA_OVERHEAD);s->received=counter;return n-LA_OVERHEAD;
}
