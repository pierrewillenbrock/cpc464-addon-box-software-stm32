
#pragma once

#include <stdint.h>

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
