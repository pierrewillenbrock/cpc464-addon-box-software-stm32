
#pragma once

#include <usbproto/usb.h> //for PACKED, stdint

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
