
#pragma once

class USBDevice;

struct USBEndpoint {
	enum { HostToDevice, DeviceToHost } direction;
	enum { Control, Bulk, ISO, IRQ } type;
	uint8_t index;
	uint16_t max_packet_length;
	USBDevice &device;
	USBEndpoint(USBDevice &dev) : device(dev) {}
};
