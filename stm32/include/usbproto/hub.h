
#pragma once

#include <usbproto/usb.h> //for PACKED, stdint

struct USBDescriptorHUB {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bNbrPorts;
	uint16_t wHubCharacteristics;
	uint8_t bPwrOn2PwrGood;
	uint8_t bHubContrCurrent;
	uint8_t DeviceRemovable[0];//variable sized, padded to 8bit alignment
	//uint8_t PortPwrCtrlMask[0];
} PACKED;

