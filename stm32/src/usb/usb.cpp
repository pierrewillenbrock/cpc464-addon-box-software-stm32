
#include <usb/usb.h>

#include <bsp/stm32f4xx.h>
#include <bsp/stm32f4xx_rcc.h>
#include <irq.h>
#include <timer.h>
#include <lang.hpp>
#include <bits.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <deque>
#include <vector>
#include <array>
#include <string>
#include <unordered_map>

#include "usbdev.h"
#include "usbproto.h"
#include "usbchannel.hpp"
#include "usbendpoint.hpp"
#include "usbdevice.hpp"

static USBDevice *rootDevice = NULL;
static volatile uint8_t usb_address = 1;

struct USBDeviceActivation {
	void *data;
	void (*activate)(void *data);
};

static std::deque<URB*> USB_URBqueue;
static std::deque<USBDeviceActivation> USB_activationQueue;
static USBDeviceActivation USB_activationCurrent;
static std::array<USBChannel,12> channels({0,1,2,3,4,5,6,7,8,9,10,11});

OTG_Core_TypeDef * otgc = OTGF_CORE;
OTG_Host_TypeDef * otgh = OTGF_HOST;

static void configureFifos() {
	//RXFIFO: at least (Largest Packet Size/4)+1,
	//recommeded twice of that. we should be quick enough to
	//move all data of one packet to the cpu in the time it takes
	//usb to fill another, so use that. largest packet size is 1023,
	//but we only have 1.25k RAM.
	//we have enough RAM to handle a full frame time, but barely. and we
	//need to subtract some for periodic and non-periodic tx.
	//so, we could do it somewhat dynamic. allocate space for periodic tx,
	//then allocate space for periodic rx, then split the rest between
	//rx and non periodic tx, maybe 1:1?
	size_t periodic_rx_words = 0;//including the status words
	size_t periodic_tx_words = 0;//including the status words
	size_t fifo_size = 320;
	size_t ptx_size = periodic_tx_words;
	size_t rx_size = periodic_rx_words;
	if (rx_size < 16)
		rx_size = 16;
	if (ptx_size < 16)
		ptx_size = 16;
	size_t nptx_size = (fifo_size - ptx_size - rx_size)/2;
	if (nptx_size < 16) {
		nptx_size = 16;
		if (ptx_size + nptx_size + rx_size > fifo_size) {
			//try to steal from rx_size first.
			rx_size = fifo_size - ptx_size - nptx_size;
			if (rx_size < 16) {
				rx_size = 16;
				ptx_size = fifo_size - rx_size - nptx_size;
			}
		}
	}
	rx_size = fifo_size - ptx_size - nptx_size;
	otgc->GRXFSIZ = rx_size; //todo: check if this is << 0 or << 16 (like the others)
	otgc->HNPTXFSIZ = (nptx_size << 16) | (rx_size << 0);
	otgc->HPTXFSIZ = (ptx_size << 16) | ((rx_size + nptx_size) << 0);
}

static void USB_RegInit() {
	otgc->GUSBCFG = OTG_GUSBCFG_PHYSEL;

	otgc->GRSTCTL |= OTG_GRSTCTL_CSRST;
	while(otgc->GRSTCTL & OTG_GRSTCTL_CSRST) {}

	otgc->GINTSTS = 0xf030fc0a;
	//GINTMSK=1 => unmasked, periodic txfifo irq on empty
	otgc->GAHBCFG = OTG_GAHBCFG_GINTMSK;
	//GUSBCFG: HNP, SRP bit, FS timeout calibration, USB turnaround
	otgc->GUSBCFG &= ~OTG_GUSBCFG_TOCAL_MASK;
	otgc->GUSBCFG |= (4 << OTG_GUSBCFG_TOCAL_SHIFT);
	//force host mode
	otgc->GUSBCFG |= OTG_GUSBCFG_FHMOD;


	//check CMOD in GINTSTS for correct mode
	while((otgc->GINTSTS & OTG_GINTSTS_CMOD) !=
	      OTG_GINTSTS_CMOD) {}
	otgc->GCCFG |= OTG_GCCFG_NOVBUSSENS;
	otgc->GCCFG |= OTG_GCCFG_PWRDWN;

	otgh->HPRT |= OTG_HPRT_PPWR;

	//setup fifos
	configureFifos();

	//host mode initialisation
	//GINTMSK: HPRTINT
	otgc->GINTMSK |= OTG_GINTMSK_PRTIM
		| OTG_GINTMSK_HCIM;
}

void USB_Setup() {
	//setup rcc
	RCC_AHB2PeriphClockCmd(RCC_AHB2Periph_OTG_FS, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);

	//setup gpio
	GPIO_InitTypeDef gpio_init;
	GPIO_StructInit(&gpio_init);
	gpio_init.GPIO_Pin = GPIO_Pin_11 | GPIO_Pin_12;
	gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_AF;
	GPIO_Init(GPIOA, &gpio_init);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource11, GPIO_AF_OTG_FS);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource12, GPIO_AF_OTG_FS);

	NVIC_InitTypeDef nvicinit;
	nvicinit.NVIC_IRQChannel = OTG_FS_IRQn;
	nvicinit.NVIC_IRQChannelPreemptionPriority = 3;
	nvicinit.NVIC_IRQChannelSubPriority = 3;
	nvicinit.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&nvicinit);

	otgh->HCFG = OTG_HCFG_FSLSPCS_48MHZ;
	otgh->HFIR = 48000;

	USB_RegInit();
}

/*
void OTG_FS_WKUP_IRQHandler() { //do we need this one?
}
*/

static uint32_t timer_handle = 0;

static void USB_PortResetTimer(void *unused) {
	//okay, we held reset for long enough.
	otgh->HPRT &= ~OTG_HPRT_PRST;
	//irq getting emitted now.
}

void USB_submitURB(URB *u) {
	//so, what do we do here?
	//check if we have a free channel. if not => queue it.
	//allocate said channel and prepare it
	//copy data to the right places?
	//trigger channel?
	ISR_Guard g;
	for(auto &ch : channels) {
		if (ch.isUnused()) {
			ch.setupForURB(u);
			return;
		}
	}
	//waiting for a free channel
	USB_URBqueue.push_back(u);
}

URB *USB_getNextURB() {
	ISR_Guard g;
	URB *u = NULL;
	if (!USB_URBqueue.empty()) {
		u = USB_URBqueue.front();
		USB_URBqueue.pop_front();
	}
	return u;
}

uint8_t USB_getNextAddress() {
	ISR_Guard g;
	uint8_t addr = usb_address;
	if (usb_address >= 127)
		usb_address = 1;
	else
		usb_address++;
	//todo: need to check if the address is unused, and continue to scan
	//if not.
}

static void USB_queueDeviceActivation(void (*activate)(void*data),void *data) {
	bool directActivate = false;
	{
		ISR_Guard g;
		if (USB_activationCurrent.activate == NULL) {
			USB_activationCurrent.activate = activate;
			USB_activationCurrent.data = data;
			directActivate = true;
		} else {
			USBDeviceActivation a;
			a.activate = activate;
			a.data = data;
			USB_activationQueue.push_back(a);
		}
	}
	if (directActivate)
		activate(data);
}

void USB_activationComplete(uint8_t address) {
	//todo: note that we have a new device. maybe also store a handle
	//to the device here? removal of a device is not handled yet!
	{
		ISR_Guard g;
		if (USB_activationQueue.empty()) {
			USB_activationCurrent.activate = NULL;
			USB_activationCurrent.data = NULL;
			return;
		}
		USB_activationCurrent = USB_activationQueue.front();
		USB_activationQueue.pop_front();
	}
	USB_activationCurrent.activate(USB_activationCurrent.data);
}

static void activateRootDevice(void *unused) {
	//the irq handler does most of the work for us here,
	//but an usb hub driver would have to queue port enable,
	//device reset and only then create the device.
	if ((otgh->HPRT & OTG_HPRT_PSPD_MASK) ==
	    OTG_HPRT_PSPD_FS) {
		rootDevice = new USBDevice(USBSpeed::Full);
	} else {
		rootDevice = new USBDevice(USBSpeed::Low);
	}
	rootDevice->activate();
}

void OTG_FS_IRQHandler() {
	//this function serves three purposes:
	//* handle the root port interrupts
	//* handle channel interrupts(by passing them off to the channels)
	//* handle fifo interrupts(by passing them off to the channel(s))
	uint32_t gintsts = otgc->GINTSTS & otgc->GINTMSK;
	if (gintsts & OTG_GINTSTS_HPRTINT) {
		if (otgh->HPRT & OTG_HPRT_PCDET) {
			//clear irq, start reset
			//do we have to wait for 100ms here per spec 9.1.2,
			//or does the controller wait for us?
			uint32_t hprt = otgh->HPRT;
			hprt &= ~(OTG_HPRT_PENCHNG|OTG_HPRT_PENA|OTG_HPRT_PCDET);
			hprt |= OTG_HPRT_PCDET | OTG_HPRT_PRST;
			otgh->HPRT = hprt;
			timer_handle = Timer_Oneshot(11, USB_PortResetTimer, NULL);
			return;
		}
		if (otgh->HPRT & OTG_HPRT_PENCHNG) {
			uint32_t hprt = otgh->HPRT;
			hprt &= ~(OTG_HPRT_PENCHNG|OTG_HPRT_PENA|OTG_HPRT_PCDET);
			hprt |= OTG_HPRT_PENCHNG;
			otgh->HPRT = hprt;
			if (otgh->HPRT & OTG_HPRT_PENA) {
				//port is now enabled, need to setup clocks etc and trigger enumeration
				if ((otgh->HPRT & OTG_HPRT_PSPD_MASK) ==
				    OTG_HPRT_PSPD_FS) {
					if (otgh->HCFG != OTG_HCFG_FSLSPCS_48MHZ) {
						otgh->HCFG = OTG_HCFG_FSLSPCS_48MHZ;
						otgh->HFIR = 48000;
						USB_RegInit();
						return;
					}
					//now we need to create the new device
					USB_queueDeviceActivation(activateRootDevice, NULL);
				} else if ((otgh->HPRT & OTG_HPRT_PSPD_MASK) ==
				    OTG_HPRT_PSPD_LS) {
					if (otgh->HCFG != OTG_HCFG_FSLSPCS_6MHZ) {
						otgh->HCFG = OTG_HCFG_FSLSPCS_6MHZ;
						otgh->HFIR = 6000;
						USB_RegInit();
						return;
					}
					//now we need to create the new device
					USB_queueDeviceActivation(activateRootDevice, NULL);
				} else {
					assert(0);
				}
			} else {
				if (otgh->HCFG != OTG_HCFG_FSLSPCS_48MHZ) {
					otgh->HCFG = OTG_HCFG_FSLSPCS_48MHZ;
					otgh->HFIR = 48000;
					USB_RegInit();
				}
				rootDevice->state = USBDevice::Disconnected;
				delete rootDevice;
				rootDevice = NULL;
			}
			return;
		}
	}
	if (gintsts & OTG_GINTSTS_PTXFE) {
		for(auto &ch : channels)
			ch.PTXPossible();
		//if it did not clear PTXFE, mask it.
		if (otgc->GINTSTS & OTG_GINTSTS_PTXFE)
			otgc->GINTMSK &= ~OTG_GINTMSK_PTXFEM;
		return;
	}
	if (gintsts & OTG_GINTSTS_NPTXFE) {
		for(auto &ch : channels)
			ch.NPTXPossible();
		//if it did not clear NPTXFE, mask it.
		if (otgc->GINTSTS & OTG_GINTSTS_NPTXFE)
			otgc->GINTMSK &= ~OTG_GINTMSK_NPTXFEM;
		return;
	}
	if (gintsts & OTG_GINTSTS_RXFLVL) {
		//got a packet in RXFIFO. yay. find the channel it belongs to.
		uint32_t grxsts = otgc->GRXSTSP;
		unsigned channel = grxsts & OTG_GRXSTSR_CHNUM_MASK;
		uint32_t pktsts = (grxsts & OTG_GRXSTSR_PKTSTS_MASK) >> OTG_GRXSTSR_PKTSTS_SHIFT;
		if (pktsts == 2) {
			channels[channel].RXData(
				(grxsts & OTG_GRXSTSR_BCNT_MASK) >> OTG_GRXSTSR_BCNT_SHIFT,
				(grxsts & OTG_GRXSTSR_DPID_MASK) >> OTG_GRXSTSR_DPID_SHIFT);
		}
		return;
	}
	if (gintsts & OTG_GINTSTS_HCINT) {
		//find the channels
		uint32_t haint = otgh->HAINT;
		for(unsigned i = 0; i < channels.size(); i++) {
			if (haint & 1)
				channels[i].INT();
			haint >>= 1;
		}
		return;
	}
	assert(0);
}
