
#pragma once

#include <bsp/stm32f4xx.h>

namespace usb {
	struct URB;
	class Device;
	struct Endpoint;

	extern unsigned int frameChannelTime;

	URB *getNextNonperiodicURB();
	URB *getNextPeriodicURB();
	URB *getNextURB();
	void activationComplete();
	void deactivateAddress(uint8_t address);
	uint8_t getNextAddress();
	void registerDevice(Device *device);
	void unregisterDevice(Device *device);
	void queueDeviceActivation(void (*activate)(void*data),void *data);
	void killEndpoint(Endpoint *endpoint);
	unsigned int frameTimeRemaining();

}

static OTG_Core_TypeDef * const otgc = OTGF_CORE;
static OTG_Host_TypeDef * const otgh = OTGF_HOST;
