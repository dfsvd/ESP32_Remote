#pragma once
#include <stdint.h>

#define TILE_COUNT 222

typedef struct {
    uint8_t z;
    uint8_t _pad;
    uint16_t _pad2;
    uint32_t x;
    uint32_t y;
    uint32_t offset;
    uint32_t size;
} tile_entry_t;

extern const tile_entry_t tile_entries[TILE_COUNT];
extern const uint8_t tile_pool[];
extern const unsigned int tile_pool_size;
const uint8_t *tile_find(uint8_t z, uint32_t x, uint32_t y, uint32_t *out_size);
