
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

#define USBHID_MAINFLAG_CONSTANT       0x001
#define USBHID_MAINFLAG_VARIABLE       0x002
#define USBHID_MAINFLAG_RELATIVE       0x004
#define USBHID_MAINFLAG_WRAP           0x008
#define USBHID_MAINFLAG_NON_LINEAR     0x010
#define USBHID_MAINFLAG_NO_PREFERRED   0x020
#define USBHID_MAINFLAG_NULL_STATE     0x040
#define USBHID_MAINFLAG_NON_VOLATILE   0x080
#define USBHID_MAINFLAG_BUFFERED_BYTES 0x100
