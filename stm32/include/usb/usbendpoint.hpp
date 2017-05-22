
#pragma once

#include <refcounted.hpp>

namespace usb {
	class Device;

	struct Endpoint : public RefcountProxy<Device> {
		enum { HostToDevice, DeviceToHost } direction;
		enum { Control, Bulk, ISO, IRQ } type;
		uint8_t address;
		uint16_t max_packet_length;
		bool dataToggleIN;
		bool dataToggleOUT;
		Device &device;
		Endpoint ( Device &dev)
		: RefcountProxy(dev)
		, device(dev)
		{}
	};
}

