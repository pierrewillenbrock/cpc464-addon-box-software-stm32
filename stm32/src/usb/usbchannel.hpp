
#pragma once

#include <stdint.h>
#include <sys/types.h>

struct URB;
struct OTG_Host_Channel_TypeDef;

struct USBChannel {
private:
	enum { Unused, TXWait, TXResult,
	       RXWait,
	       CtlSetupTXWait, CtlSetupTXResult,
	       CtlDataTXWait, CtlDataTXResult,
	       CtlDataRXWait,
	       CtlStatusTXWait, CtlStatusTXResult,
	       CtlStatusRXWait,
	       Disabling,
	       DisablingPeriodic,
	       PeriodicWait
	} state;
	OTG_Host_Channel_TypeDef *regs;
	volatile uint32_t *channeldata;
	URB *current_urb;
	uint32_t *data;
	size_t data_remaining;
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
};

