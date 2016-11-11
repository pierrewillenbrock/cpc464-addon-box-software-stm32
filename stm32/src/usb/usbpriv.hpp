
#pragma once

struct URB;

URB *USB_getNextURB();
void USB_activationComplete(uint8_t address);
uint8_t USB_getNextAddress();

