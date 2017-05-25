
#pragma once

#include <stdint.h>
#include <sys/types.h>

struct sprite_info {
	uint16_t hpos;
	uint16_t vpos;
	uint16_t map_addr;
	uint8_t hsize;
	uint8_t vsize;
	uint8_t hpitch;
	uint8_t doublesize:1;
	uint8_t reserved:7;
} __attribute__((packed));

struct SpriteVMemInfo {
	uint16_t total;
	uint16_t used;
	uint16_t free;
	uint16_t largestFreeBlock;
};

#ifdef __cplusplus
extern "C" {
#endif

void sprite_set_palette(unsigned num, uint8_t const *data);
void sprite_upload_palette();

//if addr == ~0U, looks for space anywhere in the vmem
//return ~0U if it could not find room.
unsigned sprite_alloc_vmem(size_t size, unsigned align, unsigned addr);
void sprite_free_vmem(unsigned addr);

struct SpriteVMemInfo spritevmeminfo();

#ifdef __cplusplus
}
#endif
