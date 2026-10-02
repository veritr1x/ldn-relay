/* MIT. Receive credits depend on receive capacity and an actual pending reply. */
#ifndef RELAY_ADMISSION_H
#define RELAY_ADMISSION_H
#include <stdbool.h>
#include <stddef.h>
static inline bool lr_receive_ready(size_t incoming,size_t outgoing,size_t reply_slots){
    return incoming<64 && reply_slots<=32 && (!reply_slots || outgoing<=32-reply_slots);
}
#endif
