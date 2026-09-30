#ifndef RELAY_PROTOCOL_H
#define RELAY_PROTOCOL_H
#include "relay_codec.h"

#define RELAY_VERSION "0.1.0"
#define RELAY_MAX_UDP 1400
#define RELAY_MAX_SOCKETS 4
#define RELAY_MAX_NETWORKS 24

/* Messages begin opcode:u8 request_id:u16le, followed by opcode-specific data.
 * IPv4 bytes and UDP ports use network order; other integers little-endian. */
enum {
    RL_SCAN=1, RL_JOIN=2, RL_LEAVE=3, RL_BIND=4, RL_SEND=5,
    RL_INFO=6, RL_PING=7, RL_STATS=8,
    RL_CAPS=0x80, RL_NETWORKS=0x81, RL_CONNECTED=0x82,
    RL_LEFT=0x83, RL_BOUND=0x84, RL_SENT=0x85, RL_UDP=0x86,
    RL_PONG=0x87, RL_COUNTERS=0x88, RL_ERROR=0xff
};
enum {
    RL_ERR_FORMAT=1, RL_ERR_STATE=2, RL_ERR_UNSUPPORTED=3,
    RL_ERR_NATIVE=4, RL_ERR_SOCKET=5, RL_ERR_QUEUE=6,
    RL_ERR_STALE=7, RL_ERR_DESTINATION=8
};
#endif
