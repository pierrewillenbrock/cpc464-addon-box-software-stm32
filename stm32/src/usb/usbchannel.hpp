
#pragma once

#include <stdint.h>
#include <sys/types.h>
#include <usb/usb.hpp>

struct URB;
struct OTG_Host_Channel_TypeDef;

struct USBChannel {
private:
	enum { Unused,
	       TXWait, TXResult, DisablingTX,
	       RXWait,
	       CtlSetupTXWait, CtlSetupTXResult, DisablingCtlSetupTX,
	       CtlDataTXWait, CtlDataTXResult, DisablingCtlDataTX,
	       CtlDataRXWait,
	       CtlStatusTXWait, CtlStatusTXResult, DisablingCtlStatusTX,
	       CtlStatusRXWait,
	       Disabling,
	       DisablingPeriodic,
	       PeriodicWait
	} state;
	OTG_Host_Channel_TypeDef *regs;
	volatile uint32_t *channeldata;
	URB *current_urb;
	uint32_t *data;
	size_t data_packetsize; //counted in uint32_t units
	size_t data_remaining; //counted in uint8_t units
	unsigned index;
	unsigned frameCounter;
	void doSETUPTransfer();
	void doINTransfer(void *data, size_t xfrsiz);
	void doOUTTransfer(void *data, size_t xfrsiz);
public:
	USBChannel(unsigned index);
	void setupForURB(URB *u);
	void retireURB(URB *u);
	void PTXPossible();
	void NPTXPossible();
	void RXData(unsigned bcnt, unsigned dpid);
	void INT();
	void SOF();
	bool isUnused() { return state == Unused; }
	void killEndpoint(USBEndpoint *endpoint);
};

