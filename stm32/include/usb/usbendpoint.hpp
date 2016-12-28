
#pragma once

#include <refcounted.hpp>

#include <usb/usbdevice.hpp>

struct USBEndpoint : public RefcountProxy<USBDevice> {
	enum { HostToDevice, DeviceToHost } direction;
	enum { Control, Bulk, ISO, IRQ } type;
	uint8_t address;
	uint16_t max_packet_length;
	bool dataToggleIN;
	bool dataToggleOUT;
	USBDevice &device;
	USBEndpoint(USBDevice &dev)
		: RefcountProxy(dev)
		, device(dev)
		{}
};
