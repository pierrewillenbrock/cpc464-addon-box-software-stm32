
#pragma once

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct USBEndpoint;

struct URB {
	USBEndpoint *endpoint;
	struct {  // for Control transactions
		uint8_t bmRequestType;
		uint8_t bRequest;
		uint16_t wValue;
		uint16_t wIndex;
		uint16_t wLength;
	} setup;
	void *buffer;   //for all transactions
	size_t buffer_len;
	size_t buffer_received;
	enum { Ack, Nak, Stall, Nyet } result; //for Bulk, Control and IRQ transactions
	void *userpriv;
	void (*completion)(int result, URB *u);
};

void USB_Setup();
void USB_submitURB(struct URB *u);

#ifdef __cplusplus
}
#endif
