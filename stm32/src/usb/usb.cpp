/** \todo: if we detect a hung bus (i.E. when a URB takes a long time
   to complete, like across a SOF), we set the PENA bit of HPRT.
   if there is no device connected at that point, all is fine. otherwise,
   we may have to do another bus reset cycle. need to figure that last part
   out while doing. but first, need the timeout infrastructure.
   \todo there are still initialisation troubles when devices are plugged into
   the port at startup, or when devices are plugged into the hub while it is
   getting plugged. need reproduction steps for all of the above.
 */

#include <usb/usb.hpp>

#include <bsp/stm32f4xx.h>
#include <bsp/stm32f4xx_rcc.h>
#include <irq.h>
#include <timer.h>
#include <assert.h>
#include <deque>
#include <bitset>
#include <array>
#include <map>
#include <eventlogger.hpp>

#include "usbhub.h"
#include "usbdev.h"
#include "usbchannel.hpp"
#include "usbpriv.hpp"
#include <usb/usbdevice.hpp>
#include <usb/usbendpoint.hpp>
#include <usb/urb.hpp>

static RefPtr<usb::Device> rootDevice = NULL;///< \brief The current root device.
static volatile uint8_t usb_address = 1;///< \brief the next usb_address being used

struct USBDeviceActivation {
	void *data;
	void (*activate)(void *data);
};

static std::deque<usb::URB*> USB_nonperiodicQueue;///< \brief Queue holding non-periodic URBs
static std::multimap<unsigned int,usb::URB*> USB_periodicQueue;///< \brief Ordered queue holding periodic URBs
/** \brief Queue holding activation requests
 *
 * Activations need to be serialized since all devices start out on
 * address 0 after reset+enable.
 */
static std::deque<USBDeviceActivation> USB_activationQueue;
static USBDeviceActivation USB_activationCurrent;///< \brief Current device activation
static std::array<usb::Channel,8> channels({{0,1,2,3,4,5,6,7}}); ///< \brief All USB Channels
static unsigned int frameCounter = 0;///< Number of frames since the begin of time
unsigned int usb::frameChannelTime = 0;///< Time allocated by channels

//static const unsigned int USB_frameEndTime = 480; ///< Hold off time at the end of the frame
static const unsigned int USB_frameEndTime = 24000;

static void configureFifos() {
	//RXFIFO: at least ceil(Largest Packet Size/4)+1,
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
	otgc->GRXFSIZ = rx_size; //this is << 0, unlike << 16 below.
	otgc->HNPTXFSIZ = (nptx_size << 16) | (rx_size << 0);
	otgc->HPTXFSIZ = (ptx_size << 16) | ((rx_size + nptx_size) << 0);
}

static void USB_RegInit() {
	otgc->GINTMSK = 0;

	otgc->GUSBCFG = OTG_GUSBCFG_PHYSEL;

	otgc->GRSTCTL |= OTG_GRSTCTL_CSRST;
	while(otgc->GRSTCTL & OTG_GRSTCTL_CSRST) {}

	otgc->GINTSTS = 0xf030fc0a;
	//GINTMSK=1 => unmasked, periodic txfifo irq on empty
	otgc->GAHBCFG = OTG_GAHBCFG_GINTMSK;
	//GUSBCFG: HNP, SRP bit, FS timeout calibration, USB turnaround
	//todo: TOCAL. not all that clear what goes here.
	//documentation tells us that it adds 0.25 PHY clk/lsb to the
	//default timeout, i.E. 0-1.75 PHY clks. full speed timeout is 16-18
	//bit times(inclusive). it also tells us that this depends on
	//enumerated speed.
	otgc->GUSBCFG &= ~OTG_GUSBCFG_TOCAL_MASK;
	otgc->GUSBCFG |= (4 << OTG_GUSBCFG_TOCAL_SHIFT);
	//force host mode
	otgc->GUSBCFG |= OTG_GUSBCFG_FHMOD;


	//check CMOD in GINTSTS for correct mode
	while((otgc->GINTSTS & OTG_GINTSTS_CMOD) !=
	      OTG_GINTSTS_CMOD) {}
	otgc->GCCFG |= OTG_GCCFG_NOVBUSSENS;
	otgc->GCCFG |= OTG_GCCFG_PWRDWN;

	otgh->HPRT |= OTG_HPRT_PPWR | OTG_HPRT_PENCHNG | OTG_HPRT_PENA
		| OTG_HPRT_PCDET;

	//setup fifos
	configureFifos();

	otgc->GINTSTS = 0xffffffff;
	//host mode initialisation
	//GINTMSK: HPRTINT
	otgc->GINTMSK |= OTG_GINTMSK_PRTIM
		| OTG_GINTMSK_HCIM
		| OTG_GINTMSK_RXFLVLM
		| OTG_GINTMSK_SOFM
		| OTG_GINTMSK_WUIM
		| OTG_GINTMSK_DISCINT
		| OTG_GINTMSK_IPXFRM   //once we have useful scheduling for periodic irqs, this bit serves as a good indicator of errors and should never be on.
		| OTG_GINTMSK_OTGINT
		| OTG_GINTMSK_MMISM;
}

void usb::Setup() {
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

	USB_RegInit();
	otgh->HFIR = 48000;

	USBHUB_Setup();
}

static std::vector<usb::Driver *> usb_drivers;
static std::deque<RefPtr<usb::Device> > usb_devices;

void usb::registerDevice ( usb::Device *device) {
	LogEvent("USB_registerDevice");
	ISR_Guard g;
	usb_devices.push_back(device);
	//go through the registered drivers and make them probe the device
	//until one is found that is happy with it
	for(auto &d : usb_drivers) {
		d->probe(device);
	}
}

void usb::killEndpoint ( usb::Endpoint *endpoint) {
	ISR_Guard g;
	for(auto &ch : channels)
		ch.killEndpoint(endpoint);
	for(auto it = USB_nonperiodicQueue.begin(); it != USB_nonperiodicQueue.end();) {
		if ((*it)->endpoint == endpoint)
			it = USB_nonperiodicQueue.erase(it);
		else
			it++;
	}
}

void usb::unregisterDevice ( usb::Device *device) {
	LogEvent("USB_unregisterDevice");
	ISR_Guard g;
	for(auto it = usb_devices.begin(); it != usb_devices.end(); it++) {
		if (*it == device) {
			usb_devices.erase(it);
			return;
		}
	}
}

void usb::registerDriver ( usb::Driver *driver) {
	ISR_Guard g;
	usb_drivers.push_back(driver);
	for(auto &d : usb_devices) {
		driver->probe(d);
	}
}

/*
void OTG_FS_WKUP_IRQHandler() { //do we need this one?
}
*/

/** \brief Time remaining in this frame
 *
 * \returns Time remaining in this frame in cycles of 48MHz
 */
unsigned int usb::frameTimeRemaining() {
	unsigned int mult = (otgh->HCFG == OTG_HCFG_FSLSPCS_48MHZ)?1:8;
	int res = (int)(((otgh->HFNUM & 0xffff0000) >> 16)*mult)-(int)usb::frameChannelTime-(int)USB_frameEndTime;
	if (res < 0)
		return 0;
	return res;
}

void usb::submitURB ( usb::URB *u) {
	//so, what do we do here?
	//check if we have a free channel. if not => queue it.
	//allocate said channel and prepare it
	//copy data to the right places?
	//trigger channel?
	ISR_Guard g;
	assert(u->endpoint);
	if ( usb::frameTimeRemaining() > u->thisFrameTime()) {
		for(auto &ch : channels) {
			if (ch.isUnused()) {
				ch.setupForURB(u);
				if (u->endpoint->type == usb::Endpoint::IRQ ||
					u->endpoint->type == usb::Endpoint::ISO) {
					USB_periodicQueue.insert(
						std::make_pair(frameCounter+u->pollingInterval,u));
				}
				return;
			}
		}
	}
	//waiting for a free channel
	if (u->endpoint->type == usb::Endpoint::Bulk ||
		u->endpoint->type == usb::Endpoint::Control)
		USB_nonperiodicQueue.push_back(u);
	else
		USB_periodicQueue.insert(std::make_pair(frameCounter,u));
}

void usb::retireURB (struct usb::URB *u) {
	ISR_Guard g;
	for(auto &ch : channels) {
		if (!ch.isUnused())
			//if a channel is servicing this urb, it stops doing so
			//otherwise, it does nothing.
			ch.retireURB(u);
	}
	for(auto it = USB_nonperiodicQueue.begin(); it != USB_nonperiodicQueue.end(); it++) {
		//if we found it in the queue still, just remove it.
		if (*it == u) {
			         USB_nonperiodicQueue.erase(it);
			break;
		}
	}
	for(auto it = USB_periodicQueue.begin(); it != USB_periodicQueue.end(); it++) {
		//if we found it in the queue still, just remove it.
		if (it->second == u) {
			USB_periodicQueue.erase(it);
			break;
		}
	}
}

usb::URB *usb::getNextNonperiodicURB() {
	ISR_Guard g;
	usb::URB *u = NULL;
	if (!USB_nonperiodicQueue.empty()) {
		u = USB_nonperiodicQueue.front();
		if ( usb::frameTimeRemaining() > u->thisFrameTime())
			USB_nonperiodicQueue.pop_front();
		else
			u = NULL;
	}
	return u;
}

usb::URB *usb::getNextPeriodicURB() {
	ISR_Guard g;
	usb::URB *u = NULL;
	if (!USB_periodicQueue.empty()) {
		auto it = USB_periodicQueue.begin();
		std::pair<unsigned int,usb::URB*> front = *it;
		if (front.first <= frameCounter &&
			             usb::frameTimeRemaining() > front.second->thisFrameTime()) {
			u = front.second;
			USB_periodicQueue.erase(it);
			front.first += u->pollingInterval;
			USB_periodicQueue.insert(front);
		}
	}
	return u;
}

usb::URB *usb::getNextURB() {
	usb::URB *u = usb::getNextPeriodicURB();
	if (!u)
		u = usb::getNextNonperiodicURB();
	return u;
}

void usb::queueDeviceActivation (void (*activate)(void*data),void *data) {
	LogEvent("USB_queueDeviceActivation");
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

static std::bitset<128> used_addresses;

void usb::activationComplete() {
	LogEvent("USB_activationComplete");
	{
		ISR_Guard g;
		if (USB_activationQueue.empty()) {
			USB_activationCurrent.activate = NULL;
			USB_activationCurrent.data = NULL;
		} else {
			USB_activationCurrent = USB_activationQueue.front();
			USB_activationQueue.pop_front();
		}
	}
	if (USB_activationCurrent.activate)
		USB_activationCurrent.activate(USB_activationCurrent.data);
}

uint8_t usb::getNextAddress() {
	ISR_Guard g;
	uint8_t addr = 0;
	if (usb_address == 0 || usb_address > 127)
		usb_address = 1;
	addr = usb_address;
	while(used_addresses[addr]) {
		if (usb_address >= 127)
			usb_address = 1;
		else
			usb_address++;
		addr = usb_address;
	}
	used_addresses[addr] = true;
	return addr;
}

void usb::deactivateAddress (uint8_t address) {
	ISR_Guard g;
	used_addresses[address] = false;
}

static void activateRootDevice(void */*unused*/) {
	LogEvent("USB:activateRootDevice");
	//the irq handler does most of the work for us here,
	//but an usb hub driver would have to queue port enable,
	//device reset and only then create the device.
	if ((otgh->HPRT & OTG_HPRT_PSPD_MASK) ==
	    OTG_HPRT_PSPD_FS) {
		rootDevice = new usb::Device (usb::Speed::Full);
	} else {
		rootDevice = new usb::Device (usb::Speed::Low);
	}
	rootDevice->activate();
}

static uint32_t usb_timer_handle = 0;

static void USB_PortResetTimer(void */*unused*/) {
	usb_timer_handle = 0;
	LogEvent("USB_PortResetTimer");
	//okay, we held reset for long enough.
	uint32_t hprt = otgh->HPRT;
	hprt &= ~(OTG_HPRT_PENCHNG|OTG_HPRT_PENA|OTG_HPRT_PCDET|OTG_HPRT_PRST);
	otgh->HPRT = hprt;
	//irq getting emitted now.
}

static void USB_PortResetBeginTimer(void */*unused*/) {
	LogEvent("USB_PortResetBeginTimer");
	uint32_t hprt = otgh->HPRT;
	hprt &= ~(OTG_HPRT_PENCHNG|OTG_HPRT_PENA|OTG_HPRT_PCDET);
	hprt |= OTG_HPRT_PRST;
	otgh->HPRT = hprt;
	usb_timer_handle = Timer_Oneshot(11000, USB_PortResetTimer, NULL);
}

void OTG_FS_IRQHandler() {
	//this function serves three purposes:
	//* handle the root port interrupts
	//* handle channel interrupts(by passing them off to the channels)
	//* handle fifo interrupts(by passing them off to the channel(s))
	uint32_t gintsts = otgc->GINTSTS & otgc->GINTMSK;
	if (gintsts & OTG_GINTSTS_HPRTINT) {
		gintsts &= ~OTG_GINTSTS_HPRTINT;
		if (otgh->HPRT & OTG_HPRT_PCDET) {
			LogEvent("USB: OTG_HPRT_PCDET");
			//clear irq, start reset
			//do we have to wait for 100ms here per spec 9.1.2,
			//or does the controller wait for us?
			uint32_t hprt = otgh->HPRT;
			hprt &= ~(OTG_HPRT_PENCHNG|OTG_HPRT_PENA|OTG_HPRT_PCDET);
			hprt |= OTG_HPRT_PCDET;
			otgh->HPRT = hprt;
			Timer_Cancel(usb_timer_handle);
			usb_timer_handle = Timer_Oneshot(100000, USB_PortResetBeginTimer, NULL);
			if (rootDevice) {
				rootDevice->disconnected();
				rootDevice = NULL;
			}
		}
		if (otgh->HPRT & OTG_HPRT_PENCHNG) {
			uint32_t hprt = otgh->HPRT;
			hprt &= ~(OTG_HPRT_PENCHNG|OTG_HPRT_PENA|OTG_HPRT_PCDET);
			hprt |= OTG_HPRT_PENCHNG;
			otgh->HPRT = hprt;
			if (otgh->HPRT & OTG_HPRT_PENA) {
				LogEvent("USB: OTG_HPRT_PENCHNG, PENA");
				//port is now enabled, need to setup clocks etc and trigger enumeration
				if ((otgh->HPRT & OTG_HPRT_PSPD_MASK) ==
				    OTG_HPRT_PSPD_FS) {
					if (otgh->HCFG != OTG_HCFG_FSLSPCS_48MHZ) {
						LogEvent("USB: =>48MHz");
						otgh->HCFG = OTG_HCFG_FSLSPCS_48MHZ;
						USB_RegInit();
						otgh->HFIR = 48000;
						//wait for the PCDET.
						//takes only a few 10 loops.
						while(! (otgh->HPRT & OTG_HPRT_PCDET)) { }
						//clear irq, do a reset
						//we avoid the 100ms timeout
						//for debouncing here.
						uint32_t hprt = otgh->HPRT;
						hprt &= ~(OTG_HPRT_PENCHNG|OTG_HPRT_PENA);
						hprt |= OTG_HPRT_PCDET;
						hprt |= OTG_HPRT_PRST;
						otgh->HPRT = hprt;
						Timer_Cancel(usb_timer_handle);
						usb_timer_handle = Timer_Oneshot(11000, USB_PortResetTimer, NULL);
					} else {
						//now we need to create the new device
						usb::queueDeviceActivation (activateRootDevice, NULL);
					}
				} else if ((otgh->HPRT & OTG_HPRT_PSPD_MASK) ==
				    OTG_HPRT_PSPD_LS) {
					if (otgh->HCFG != OTG_HCFG_FSLSPCS_6MHZ) {
						LogEvent("USB: =>6MHz");
						otgh->HCFG = OTG_HCFG_FSLSPCS_6MHZ;
						USB_RegInit();
						otgh->HFIR = 6000;
						//wait for the PCDET.
						//takes only a few 10 loops.
						while(! (otgh->HPRT & OTG_HPRT_PCDET)) { }
						//clear irq, do a reset
						//we avoid the 100ms timeout
						//for debouncing here.
						uint32_t hprt = otgh->HPRT;
						hprt &= ~(OTG_HPRT_PENCHNG|OTG_HPRT_PENA);
						hprt |= OTG_HPRT_PCDET;
						hprt |= OTG_HPRT_PRST;
						otgh->HPRT = hprt;
						Timer_Cancel(usb_timer_handle);
						usb_timer_handle = Timer_Oneshot(11000, USB_PortResetTimer, NULL);
					} else {
						//now we need to create the new device
						usb::queueDeviceActivation (activateRootDevice, NULL);
					}
				} else {
					assert(0);
				}
			} else {
				LogEvent("USB: OTG_HPRT_PENCHNG, !PENA");
				if (otgh->HCFG != OTG_HCFG_FSLSPCS_48MHZ) {
					LogEvent("USB: =>48MHz");
					otgh->HCFG = OTG_HCFG_FSLSPCS_48MHZ;
					USB_RegInit();
					otgh->HFIR = 48000;
				}
				if (rootDevice) {
					rootDevice->disconnected();
					rootDevice = NULL;
				}
			}
		}
	}
	if (gintsts & OTG_GINTSTS_PTXFE) {
		gintsts &= ~OTG_GINTSTS_PTXFE;
		for(auto &ch : channels)
			ch.PTXPossible();
		//if it did not clear PTXFE, mask it.
		if (otgc->GINTSTS & OTG_GINTSTS_PTXFE)
			otgc->GINTMSK &= ~OTG_GINTMSK_PTXFEM;
	}
	if (gintsts & OTG_GINTSTS_NPTXFE) {
		gintsts &= ~OTG_GINTSTS_NPTXFE;
		for(auto &ch : channels)
			ch.NPTXPossible();
		//if it did not clear NPTXFE, mask it.
		if (otgc->GINTSTS & OTG_GINTSTS_NPTXFE)
			otgc->GINTMSK &= ~OTG_GINTMSK_NPTXFEM;
	}
	if (gintsts & OTG_GINTSTS_RXFLVL) {
		gintsts &= ~OTG_GINTSTS_RXFLVL;
		//got a packet in RXFIFO. yay. find the channel it belongs to.
		uint32_t grxsts = otgc->GRXSTSP;
		unsigned channel = grxsts & OTG_GRXSTSR_CHNUM_MASK;
		uint32_t pktsts = (grxsts & OTG_GRXSTSR_PKTSTS_MASK) >> OTG_GRXSTSR_PKTSTS_SHIFT;
		if (pktsts == 2) {
			channels[channel].RXData(
				(grxsts & OTG_GRXSTSR_BCNT_MASK) >> OTG_GRXSTSR_BCNT_SHIFT,
				(grxsts & OTG_GRXSTSR_DPID_MASK) >> OTG_GRXSTSR_DPID_SHIFT);
		}
	}
	if (gintsts & OTG_GINTSTS_HCINT) {
		gintsts &= ~OTG_GINTSTS_HCINT;
		//find the channels
		uint32_t haint = otgh->HAINT;
		for(unsigned i = 0; i < channels.size(); i++) {
			if (haint & 1)
				channels[i].INT();
			haint >>= 1;
		}
	}
	if (gintsts & OTG_GINTSTS_DISCINT) {
		gintsts &= ~OTG_GINTSTS_DISCINT;
		otgc->GINTSTS = OTG_GINTSTS_DISCINT;
		LogEvent("USB: OTG_GINTSTS_DISCINT");
		//we are handling this via the host port interrupt,
		//so we could reasonable leave it masked.
	}
	if (gintsts & OTG_GINTSTS_IPXFR) {
		gintsts &= ~OTG_GINTSTS_IPXFR;
		otgc->GINTSTS = OTG_GINTSTS_IPXFR;
		LogEvent("USB: OTG_GINTSTS_IPXFR");
	}
	if (gintsts & OTG_GINTSTS_SOF) {
		gintsts &= ~OTG_GINTSTS_SOF;
		frameCounter ++;
		otgc->GINTSTS = OTG_GINTSTS_SOF;
		LogEvent("USB: SOF");
		//first, push the periodics form queue
		for(auto &ch : channels) {
			ISR_Guard g;
			if (ch.isUnused()) {
				usb::URB *u = usb::getNextPeriodicURB();
				if (u)
					ch.setupForURB(u);
			}
		}
		//then, continue any non-periodic
		for(auto &ch : channels) {
			ch.SOF();
		}
		//finally, push any non-periodic from queue.
		for(auto &ch : channels) {
			ISR_Guard g;
			if (ch.isUnused()) {
				usb::URB *u = usb::getNextNonperiodicURB();
				if (u)
					ch.setupForURB(u);
			}
		}
	}
	if (gintsts)
		assert(0);
}
