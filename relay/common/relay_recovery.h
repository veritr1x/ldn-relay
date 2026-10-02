/* MIT. Same-session GATT read recovery; no stream reset or queue clearing. */
#ifndef RELAY_RECOVERY_H
#define RELAY_RECOVERY_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool enabled,pulling,pending;
    uint64_t last_receive,next_read,requested;
    unsigned reads,timeouts;
} LrRecovery;
static inline void lr_recovery_init(LrRecovery *r,bool enabled,uint64_t now){
    *r=(LrRecovery){.enabled=enabled,.last_receive=now};
}
static inline void lr_recovery_received(LrRecovery *r,uint64_t now){r->last_receive=now;r->pending=false;}
static inline bool lr_recovery_due(LrRecovery *r,uint64_t now){
    if(!r->enabled)return false;
    if(!r->pulling && now-r->last_receive<500)return false;
    if(r->pending){if(now-r->requested<500)return false;r->pending=false;r->timeouts++;}
    return now>=r->next_read;
}
static inline void lr_recovery_requested(LrRecovery *r,bool accepted,uint64_t now){
    r->next_read=now+(accepted?25:100);
    if(accepted){r->pulling=r->pending=true;r->requested=now;r->reads++;}
}
#endif
