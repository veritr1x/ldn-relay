#ifndef RELAY_CODEC_H
#define RELAY_CODEC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LR_MAX_MESSAGE 2048
#define LR_QUEUE_DEPTH 24
#define LR_MAX_FRAME 500
#define LR_HEADER_SIZE 16
#define LR_VERSION 1

typedef struct { uint16_t size; uint8_t bytes[LR_MAX_MESSAGE]; } LrMessage;
typedef struct {
    LrMessage queue[LR_QUEUE_DEPTH];
    unsigned head, count;
    uint16_t frame_limit, tx_seq, rx_ack, tx_offset, tx_part;
    uint16_t rx_size, rx_used;
    uint8_t rx_bytes[LR_MAX_MESSAGE];
    uint64_t sent_messages, received_messages, malformed_frames, duplicate_frames;
} LrCodec;

/* The receiver must consume a complete message synchronously. Return false
 * for backpressure; the final fragment will be offered again, without an ACK. */
typedef bool (*LrReceive)(void *ctx, const uint8_t *bytes, size_t size);
void lr_init(LrCodec *c, uint16_t frame_limit);
bool lr_enqueue(LrCodec *c, const void *bytes, size_t size);
size_t lr_frame(LrCodec *c, void *out, size_t capacity);
bool lr_ingest(LrCodec *c, const void *frame, size_t size, LrReceive receive, void *ctx);
uint16_t lr_get16(const uint8_t *p);
uint32_t lr_get32(const uint8_t *p);
uint64_t lr_get64(const uint8_t *p);
void lr_put16(uint8_t *p, uint16_t value);
void lr_put32(uint8_t *p, uint32_t value);
void lr_put64(uint8_t *p, uint64_t value);
#endif
