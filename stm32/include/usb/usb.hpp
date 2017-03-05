
#pragma once

#include <stdint.h>
#include <sys/types.h>

#include <refcounted.hpp>

struct USBEndpoint;

//at this point, i'd assume c-code is out.
struct URB {
	RefPtr<USBEndpoint> endpoint;
	struct {  // for Control transactions
		uint8_t bmRequestType;
		uint8_t bRequest;
		uint16_t wValue;
		uint16_t wIndex;
		uint16_t wLength;
	} setup;
	unsigned pollingInterval;//for IRQ and ISO transactions
	void *buffer;   //for all transactions
	size_t buffer_len;
	size_t buffer_received;
	enum { Ack, Nak, Stall, Nyet, TXErr, DTErr } result; //for Bulk, Control and IRQ transactions
	void *userpriv;
	void (*completion)(int result, URB *u);
};

class USBDevice;

class USBDriver {
public:
	virtual bool probe(RefPtr<USBDevice> device) = 0;
};

class USBDriverDevice {
public:
	//called once the interface is ready for use
	virtual void interfaceClaimed(uint8_t interfaceNumber, uint8_t alternateSetting) {};
	virtual void deviceClaimed() {};
	//called only for the driver that claimed it. the driver is supposed to
	//drop all references to the usbdevice.
	virtual void disconnected(RefPtr<USBDevice> device) = 0;
};

void USB_Setup();
void USB_submitURB(struct URB *u);
void USB_retireURB(struct URB *u);
void USB_registerDriver(USBDriver *driver);
