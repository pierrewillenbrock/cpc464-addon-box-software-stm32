
#pragma once

struct URB;
class USBDevice;

URB *USB_getNextURB();
void USB_activationComplete();
void USB_deactivateAddress(uint8_t address);
uint8_t USB_getNextAddress();
void USB_registerDevice(USBDevice *device);
void USB_unregisterDevice(USBDevice *device);
void USB_queueDeviceActivation(void (*activate)(void*data),void *data);
void USB_killEndpoint(USBEndpoint *endpoint);

