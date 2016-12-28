
#pragma once

#include <stdint.h>

#ifndef PACKED
#define PACKED __attribute__((packed))
#endif

struct USBHIDDescriptorHID {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdHID;
	uint8_t bCountryCode;
	uint8_t bNumDescriptors;
	struct {
		uint8_t bDescriptorType;
		uint16_t wDescriptorLength;
	} PACKED subDescriptors[0];
} PACKED;
