
#pragma once

#include <bsp/stm32f4xx.h>

struct URB;
class USBDevice;

extern unsigned int USB_frameChannelTime;

URB *USB_getNextNonperiodicURB();
URB *USB_getNextPeriodicURB();
URB *USB_getNextURB();
void USB_activationComplete();
void USB_deactivateAddress(uint8_t address);
uint8_t USB_getNextAddress();
void USB_registerDevice(USBDevice *device);
void USB_unregisterDevice(USBDevice *device);
void USB_queueDeviceActivation(void (*activate)(void*data),void *data);
void USB_killEndpoint(USBEndpoint *endpoint);
unsigned int USB_frameTimeRemaining();

static OTG_Core_TypeDef * const otgc = OTGF_CORE;
static OTG_Host_TypeDef * const otgh = OTGF_HOST;
