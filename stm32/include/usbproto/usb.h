
#pragma once

#include <stdint.h>

#ifndef PACKED
#define PACKED __attribute__((packed))
#endif

struct USBDescriptorDevice {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdUSB;
	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t iManufacturer;
	uint8_t iProduct;
	uint8_t iSerialNumber;
	uint8_t bNumConfigurations;
} PACKED;

struct USBDescriptorString {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t unicodeChars[0];
} PACKED;

struct USBDescriptorString0 {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wLANGID[0];
} PACKED;

struct USBDescriptorConfiguration {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wTotalLength;
	uint8_t bNumInterfaces;
	uint8_t bConfigurationValue;
	uint8_t iConfiguration;
} PACKED;

struct USBDescriptorInterface {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bInterfaceNumber;
	uint8_t bAlternateSetting;
	uint8_t bNumEndpoints;
	uint8_t bInterfaceClass;
	uint8_t bInterfaceSubClass;
	uint8_t bInterfaceProtocol;
	uint8_t iInterface;
} PACKED;

struct USBDescriptorEndpoint {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bEndpointAddress;
	uint8_t bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t bInterval;
} PACKED;

#define USB_REQUEST_CLEAR_FEATURE 1
#define USB_REQUEST_SET_FEATURE 3
