
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

#define OTG_GAHBCFG_PTXFELVL (0x100)
#define OTG_GAHBCFG_GINTMSK (0x1)

#define OTG_GINTSTS_PTXFE (0x4000000)
#define OTG_GINTSTS_HCINT (0x2000000)
#define OTG_GINTSTS_HPRTINT (0x1000000)
#define OTG_GINTSTS_NPTXFE (0x20)
#define OTG_GINTSTS_RXFLVL (0x10)
#define OTG_GINTSTS_CMOD (0x1)

#define OTG_GINTMSK_PTXFEM (0x4000000)
#define OTG_GINTMSK_HCIM (0x2000000)
#define OTG_GINTMSK_PRTIM (0x1000000)
#define OTG_GINTMSK_NPTXFEM (0x20)
#define OTG_GINTMSK_RXFLVLM (0x10)
#define OTG_GINTMSK_OTGINT (0x4)
#define OTG_GINTMSK_MMISM (0x2)

#define OTG_GUSBCFG_FHMOD (0x20000000)
#define OTG_GUSBCFG_PHYSEL (0x40)
#define OTG_GUSBCFG_TOCAL_MASK (0x7)
#define OTG_GUSBCFG_TOCAL_SHIFT (0)

#define OTG_GCCFG_NOVBUSSENS (0x00200000)
#define OTG_GCCFG_PWRDWN (0x10000)

#define OTG_GRSTCTL_CSRST (0x1)

#define OTG_HPTXSTS_PTXFSAV_MASK (0xffff)
#define OTG_HPTXSTS_PTXQSAV_MASK (0xff0000)
#define OTG_HPTXSTS_PTXQSAV_SHIFT 16
#define OTG_GNPTXSTS_NPTXFSAV_MASK (0xffff)
#define OTG_GNPTXSTS_NPTXQSAV_MASK (0xff0000)
#define OTG_GNPTXSTS_NPTXQSAV_SHIFT 16

#define OTG_GRXSTSR_PKTSTS_MASK (0x1e0000)
#define OTG_GRXSTSR_PKTSTS_SHIFT 17
#define OTG_GRXSTSR_DPID_MASK (0x18000)
#define OTG_GRXSTSR_DPID_SHIFT 15
#define OTG_GRXSTSR_BCNT_MASK (0x7ff0)
#define OTG_GRXSTSR_BCNT_SHIFT 4
#define OTG_GRXSTSR_CHNUM_MASK (0xf)

#define OTG_HCFG_FSLSPCS_48MHZ (0x1)
#define OTG_HCFG_FSLSPCS_6MHZ (0x2)

#define OTG_HPRT_PSPD_MASK (0x60000)
#define OTG_HPRT_PSPD_FS (0x20000)
#define OTG_HPRT_PSPD_LS (0x40000)
#define OTG_HPRT_PPWR (0x1000)
#define OTG_HPRT_PRST (0x100)
#define OTG_HPRT_PENCHNG (0x8)
#define OTG_HPRT_PENA (0x4)
#define OTG_HPRT_PCDET (0x2)

#define OTG_HCCHAR_CHENA (0x80000000)
#define OTG_HCCHAR_CHDIS (0x40000000)
#define OTG_HCCHAR_ODDFRM (0x20000000)

#define OTG_HCINT_TXERR (0x80)
#define OTG_HCINT_NAK (0x10)
#define OTG_HCINT_STALL (0x8)
#define OTG_HCINT_CHH (0x2)
#define OTG_HCINT_XFRC (0x1)

#define OTG_HCINTMSK_TXERRM (0x80)
#define OTG_HCINTMSK_NAKM (0x10)
#define OTG_HCINTMSK_STALLM (0x8)
#define OTG_HCINTMSK_CHHM (0x2)
#define OTG_HCINTMSK_XFRCM (0x1)

#define OTG_HCTSIZ_XFRSIZ_MASK (0x7ffff)

#define PACKED __attribute__((packed))

struct USBDescriptorDevice {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdUSB;
	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t iManufacturer;
	uint8_t iProduct;
	uint8_t iSerialNumber;
	uint8_t bNumConfigurations;
} PACKED;

struct USBDescriptorString {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t unicodeChars[0];
} PACKED;

struct USBDescriptorString0 {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wLANGID[0];
} PACKED;

struct USBDescriptorConfiguration {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wTotalLength;
	uint8_t bNumInterfaces;
	uint8_t bConfigurationValue;
	uint8_t iConfiguration;
} PACKED;

struct USBDescriptorInterface {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bInterfaceNumber;
	uint8_t bAlternateSetting;
	uint8_t bNumEndpoints;
	uint8_t bInterfaceClass;
	uint8_t bInterfaceSubClass;
	uint8_t bInterfaceProtocol;
	uint8_t iInterface;
} PACKED;

struct USBDescriptorEndpoint {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bEndpointAddress;
	uint8_t bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t bInterval;
} PACKED;

class USBDevice;

struct USBEndpoint {
	enum { HostToDevice, DeviceToHost } direction;
	enum { Control, Bulk, ISO, IRQ } type;
	uint8_t index;
	uint16_t max_packet_length;
	USBDevice &device;
	USBEndpoint(USBDevice &dev) : device(dev) {}
};

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

enum class USBSpeed { Full, Low };

class USBDevice {
public:
	std::vector<USBEndpoint*> endpoints;
	enum { None, Address, DescDevice8, DescDevice, Disconnected,
	       FetchManuString, FetchProdString, FetchConfigurations } state;
	USBSpeed speed;
	uint8_t address;
	bool dataToggleIN;
	bool dataToggleOUT;
	USBDescriptorDevice deviceDescriptor;
	std::string manufacturer;
	std::string product;
	std::vector<uint8_t> descriptordata;
	struct USBDeviceURB {
		USBDevice *_this;
		URB u;
	};
	USBDeviceURB urb;
	class ExtraDescriptor {
	public:
		uint8_t bDescriptorType;
		std::vector<uint8_t> descriptor;
	};
	class Endpoint {
	public:
		USBDescriptorEndpoint descriptor;
		std::vector<ExtraDescriptor> extraDescriptors;
	};
	class AlternateSetting {
	public:
		USBDescriptorInterface descriptor;
		std::unordered_map<uint8_t, Endpoint> endpoints;
		std::vector<ExtraDescriptor> extraDescriptors;
	};
	class Interface {
	public:
		std::unordered_map<uint8_t, AlternateSetting> alternateSettings;
	};
	class Configuration {
	public:
		USBDescriptorConfiguration descriptor;
		std::vector<ExtraDescriptor> extraDescriptors;
		std::unordered_map<uint8_t, Interface> interfaces;
		Configuration(std::vector<uint8_t> const &descriptors);
	};
	std::vector<Configuration> configurations;
	USBDevice(USBSpeed speed)
		: state(None)
		, speed(speed) {
		USBEndpoint *endpoint0 = new USBEndpoint(*this);
		endpoint0->direction = USBEndpoint::HostToDevice;
		endpoint0->type = USBEndpoint::Control;
		endpoint0->index = 0;
		endpoint0->max_packet_length = 8;
		endpoints.push_back(endpoint0);
	}
	~USBDevice() {
		for(auto ep : endpoints) {
			delete ep;
		}
	}
	void activate();
	void urbCompletion(int result, URB *u);
	static void _urbCompletion(int result, URB *u);
	void prepareStringFetch(uint8_t id);
	void prepareConfigurationFetch(uint8_t id);
};

USBDevice::Configuration::Configuration(std::vector<uint8_t> const &descriptors) {
	uint8_t const *dp = descriptors.data();
	uint8_t const *de = dp + descriptors.size();
	memcpy(&descriptor, dp, sizeof(descriptor));
	dp += descriptor.bLength;
	uint8_t currentInterface = 0;
	uint8_t currentAlternateSetting = 0;
	uint8_t currentEndpoint = 0;
	enum { InConfiguration, InInterface, InEndpoint }
	state = InConfiguration;
	while(dp < de) {
		if (dp[1] == 4) {
			//Interface
			USBDescriptorInterface const *d =
				(USBDescriptorInterface *)dp;
			currentInterface = d->bInterfaceNumber;
			currentAlternateSetting = d->bAlternateSetting;
			AlternateSetting &as =
				interfaces[currentInterface].
				alternateSettings[currentAlternateSetting];
			memcpy(&as.descriptor,dp, sizeof(USBDescriptorInterface));
			state = InInterface;
		} else if (dp[1] == 5) {
			//Interface
			USBDescriptorEndpoint const *d =
				(USBDescriptorEndpoint *)dp;
			currentEndpoint = d->bEndpointAddress;
			Endpoint &ep =
				interfaces[currentInterface].
				alternateSettings[currentAlternateSetting].
				endpoints[currentEndpoint];
			memcpy(&ep.descriptor,dp, sizeof(USBDescriptorEndpoint));
			state = InEndpoint;
		} else {
			ExtraDescriptor e;
			e.bDescriptorType = dp[1];
			e.descriptor.resize(dp[0]);
			memcpy(e.descriptor.data(), dp, dp[0]);
			switch(state) {
			case InConfiguration:
				extraDescriptors.push_back(e);
				break;
			case InInterface:
				interfaces[currentInterface].
					alternateSettings[currentAlternateSetting].
					extraDescriptors.push_back(e);
				break;
			case InEndpoint:
				interfaces[currentInterface].
					alternateSettings[currentAlternateSetting].
					endpoints[currentEndpoint].
					extraDescriptors.push_back(e);
				break;
			}
		}
		dp += dp[0];//bLength
	}
}

static USBDevice *rootDevice = NULL;
static volatile uint8_t usb_address = 1;

struct USBChannel {
	enum { Unused, TXWait, TXResult,
	       RXWait,
	       CtlSetupTXWait, CtlSetupTXResult,
	       CtlDataTXWait, CtlDataTXResult,
	       CtlDataRXWait,
	       CtlStatusTXWait, CtlStatusTXResult,
	       CtlStatusRXWait,
	       Disabling
	} state;
	OTG_Host_Channel_TypeDef *regs;
	volatile uint32_t *channeldata;
	URB *current_urb;
	uint32_t *data;
	size_t data_remaining;
	unsigned index;
	USBChannel() : state(Unused) {}
	void setupForURB(URB *u);
	void doSETUPTransfer();
	void doINTransfer(void *data, size_t xfrsiz);
	void doOUTTransfer(void *data, size_t xfrsiz);
	void PTXPossible();
	void NPTXPossible();
	void RXData(unsigned bcnt, unsigned dpid);
	void INT();
};

struct USBDeviceActivation {
	void *data;
	void (*activate)(void *data);
};

static std::deque<URB*> USB_URBqueue;
static std::deque<USBDeviceActivation> USB_activationQueue;
static USBDeviceActivation USB_activationCurrent;
static std::array<USBChannel,12> channels;

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
	//todo: the HFIR is 60000. our PHY clock is only 48MHz, so should it be 48000?
	//setup static data
	for(unsigned i = 0; i < channels.size(); i++) {
		channels[i].regs = &otgh->channels[i];
		channels[i].channeldata = OTGF_EP->endpoints[i].DFIFO;
		channels[i].index = i;
	}

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

static void USB_activationComplete();

void USBDevice::prepareStringFetch(uint8_t id) {

	urb.u.setup.bmRequestType = 0x80;
	urb.u.setup.bRequest = 6;
	urb.u.setup.wValue = (3 << 8) | id;//STRING descriptor type, index id
	urb.u.setup.wIndex = 0;//default language? otherwise, we need to pull index 0 first to get the list of supported LANGIDs, or use a constant default here.
	urb.u.setup.wLength = 7; //for now, just pull 7. there is enough info in there to determine the size.
	descriptordata.resize(7);
	urb.u.buffer = descriptordata.data();
	urb.u.buffer_len = descriptordata.size();
}

void USBDevice::prepareConfigurationFetch(uint8_t id) {

	urb.u.setup.bmRequestType = 0x80;
	urb.u.setup.bRequest = 6;
	urb.u.setup.wValue = (2 << 8) | id;//CONFIGURATION descriptor type, index id
	urb.u.setup.wIndex = 0;
	urb.u.setup.wLength = 7; //for now, just pull 7. there is enough info in there to determine the size.
	descriptordata.resize(7);
	urb.u.buffer = descriptordata.data();
	urb.u.buffer_len = descriptordata.size();
}

void USBDevice::urbCompletion(int result, URB *u) {
	if (result != 0) {
		//try again
		USB_submitURB(u);
		return;
	}
	switch(state) {
	case Address: {
		address = u->setup.wValue;
		//address has been handled, can activate another device.
		USB_activationComplete();
		//now the device is in "Address" mode. need to fetch the
		//configurations and let a driver select one. then we go to "Configured"
		u->setup.bmRequestType = 0x80;
		u->setup.bRequest = 6;
		u->setup.wValue = (1 << 8) | 0;//DEVICE descriptor type, index 0
		u->setup.wIndex = 0;//no language
		u->setup.wLength = 8; //for now, just pull 8. need to increase this for the full descriptor.
		descriptordata.resize(8);
		u->buffer = descriptordata.data();
		u->buffer_len = descriptordata.size();

		state = DescDevice8;
		USB_submitURB(u);
		break;
	}
	case DescDevice8: {
		USBDescriptorDevice *d = (USBDescriptorDevice*)
			descriptordata.data();
		endpoints[0]->max_packet_length = d->bMaxPacketSize0;

		u->setup.wLength = d->bLength;
		descriptordata.resize(d->bLength);
		u->buffer = descriptordata.data();
		u->buffer_len = descriptordata.size();

		state = DescDevice;
		USB_submitURB(u);
		break;
	}
	case DescDevice:
		deviceDescriptor = *(USBDescriptorDevice*)
			descriptordata.data();

		if (deviceDescriptor.iManufacturer) {
			prepareStringFetch(deviceDescriptor.iManufacturer);
			state = FetchManuString;
			USB_submitURB(&urb.u);
		} else if (deviceDescriptor.iProduct) {
			prepareStringFetch(deviceDescriptor.iProduct);
			state = FetchProdString;
			USB_submitURB(&urb.u);
		} else if (deviceDescriptor.bNumConfigurations > 0) {
		        prepareConfigurationFetch(configurations.size());
			state = FetchConfigurations;
			USB_submitURB(&urb.u);
		} else {
			descriptordata.clear();
			descriptordata.shrink_to_fit();
			//todo: call into driver infrastructure
		}
		break;
	case FetchManuString: {
		USBDescriptorString *d = (USBDescriptorString *)
			descriptordata.data();
		if (d->bLength > descriptordata.size()) {
			u->setup.wLength = d->bLength;
			descriptordata.resize(d->bLength);
			u->buffer = descriptordata.data();
			u->buffer_len = descriptordata.size();
			USB_submitURB(&urb.u);
			break;
		}

		manufacturer = Utf16ToUtf8(
			std::basic_string<uint16_t>(d->unicodeChars,
						    (d->bLength-2)/2));

		if (deviceDescriptor.iProduct) {
			prepareStringFetch(deviceDescriptor.iProduct);
			state = FetchProdString;
			USB_submitURB(&urb.u);
		} else if (deviceDescriptor.bNumConfigurations > configurations.size()) {
		        prepareConfigurationFetch(configurations.size());
			state = FetchConfigurations;
			USB_submitURB(&urb.u);
		} else {
			descriptordata.clear();
			descriptordata.shrink_to_fit();
			//todo: call into driver infrastructure
		}
		break;
	}
	case FetchProdString: {
		USBDescriptorString *d = (USBDescriptorString *)
			descriptordata.data();
		if (d->bLength > descriptordata.size()) {
			u->setup.wLength = d->bLength;
			descriptordata.resize(d->bLength);
			u->buffer = descriptordata.data();
			u->buffer_len = descriptordata.size();
			USB_submitURB(&urb.u);
			break;
		}

		product = Utf16ToUtf8(
			std::basic_string<uint16_t>(d->unicodeChars,
						    (d->bLength-2)/2));

		if (deviceDescriptor.bNumConfigurations > configurations.size()) {
		        prepareConfigurationFetch(configurations.size());
			state = FetchConfigurations;
			USB_submitURB(&urb.u);
		} else {
			descriptordata.clear();
			descriptordata.shrink_to_fit();
			//todo: call into driver infrastructure
		}
		break;
	}
	case FetchConfigurations: {
		USBDescriptorConfiguration *d = (USBDescriptorConfiguration *)
			descriptordata.data();
		if (d->wTotalLength > descriptordata.size()) {
			u->setup.wLength = d->wTotalLength;
			descriptordata.resize(d->wTotalLength);
			u->buffer = descriptordata.data();
			u->buffer_len = descriptordata.size();
			USB_submitURB(&urb.u);
			break;
		}

		configurations.push_back(Configuration(descriptordata));

		if (deviceDescriptor.bNumConfigurations > configurations.size()) {
		        prepareConfigurationFetch(configurations.size());
			state = FetchConfigurations;
			USB_submitURB(&urb.u);
		} else {
			descriptordata.clear();
			descriptordata.shrink_to_fit();
			//todo: call into driver infrastructure
		}
		break;
	}
	}
}

void USBDevice::_urbCompletion(int result, URB *u) {
	USBDeviceURB *du = container_of(u, USBDeviceURB, u);
	du->_this->urbCompletion(result, u);
}

void USBDevice::activate() {
	//device now is in "Default" state.
	//give it an address.
	//then prepare an URB to switch to that address
	address = 0; //default address
	state = USBDevice::Address;
	urb._this = this;
	urb.u.endpoint = endpoints[0];
	urb.u.setup.bmRequestType = 0x00;
	urb.u.setup.bRequest = 5;//SET ADDRESS
	{
		ISR_Guard g;
		//todo: move the address generation somewhere else?
		urb.u.setup.wValue = usb_address++;
	}
	urb.u.setup.wIndex = 0;
	urb.u.setup.wLength = 0;
	urb.u.buffer = NULL;
	urb.u.buffer_len = 0;
	urb.u.completion = _urbCompletion;

	USB_submitURB(&urb.u);
}

void USBChannel::PTXPossible() {
	if (!(state == TXWait))
		return;
	int required_space = data_remaining;
	if (required_space > current_urb->endpoint->max_packet_length)
		required_space = current_urb->endpoint->max_packet_length;
	int available_space = otgh->HPTXSTS & OTG_HPTXSTS_PTXFSAV_MASK;
	int available_queue = otgh->HPTXSTS & OTG_HPTXSTS_PTXQSAV_MASK;
	if (available_queue == 0 ||
	    required_space > available_space)
		return;
	//we got a queue space and we got space for the data. push a packet.
	unsigned int nt = required_space >> 2;
	if (required_space & 3)
		nt++;
	for(unsigned int i = 0; i < nt; i++) {
		*channeldata = *data++;
	}
	if (data_remaining <= (nt << 2)) {
		data_remaining = 0;
		state = TXResult;
	} else {
		data_remaining -= nt << 2;
	}
}

void USBChannel::NPTXPossible() {
	if (!(state == TXWait || state == CtlSetupTXWait ||
	      state == CtlDataTXWait || state == CtlStatusTXWait))
		return;
	int required_space = data_remaining;
	if (required_space > current_urb->endpoint->max_packet_length)
		required_space = current_urb->endpoint->max_packet_length;
	int available_space = otgc->GNPTXSTS & OTG_GNPTXSTS_NPTXFSAV_MASK;
	int available_queue = otgc->GNPTXSTS & OTG_GNPTXSTS_NPTXQSAV_MASK;
	if (available_queue == 0 ||
	    required_space > available_space)
		return;
	//we got a queue space and we got space for the data. push a packet.
	unsigned int nt = required_space >> 2;
	if (required_space & 3)
		nt++;
	for(unsigned int i = 0; i < nt; i++) {
		*channeldata = *data++;
	}
	if (data_remaining <= (nt << 2)) {
		data_remaining = 0;
		switch(state) {
		case TXWait: state = TXResult; break;
		case CtlSetupTXWait: state = CtlSetupTXResult; break;
		case CtlDataTXWait: state = CtlDataTXResult; break;
		case CtlStatusTXWait: state = CtlStatusTXResult; break;
		}
	} else {
		data_remaining -= nt << 2;
	}
}

void USBChannel::RXData(unsigned bcnt, unsigned dpid) {
	if (!(state == RXWait || state == CtlDataRXWait ||
	      state == CtlStatusRXWait))
		return;
	//okay to read bcnt bytes from our fifo!
	while(bcnt > 0) {
		if (data_remaining >= 4) {
			*data++ = *channeldata;
			data_remaining -= 4;
		} else if (data_remaining > 0) {
			*data++ = *channeldata;
			data_remaining = 0;
		} else {
			(void)*channeldata;
		}
		if (bcnt >= 4)
			bcnt -= 4;
		else
			bcnt = 0;
	}
	regs->HCCHAR = regs->HCCHAR;//trigger advance of channel state
	current_urb->endpoint->device.dataToggleIN = dpid != 0x2;
}

void USBChannel::INT() {
	URB *u = current_urb;
	if (regs->HCINT & OTG_HCINT_XFRC) {
		regs->HCINT = OTG_HCINT_XFRC;
		//transfer done.
		switch(state) {
		case TXResult:
			//done.
			//the channel disabled itself at this point.
			current_urb->endpoint->device.dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
			regs->HCINTMSK &= ~(OTG_HCINTMSK_XFRCM |
					    OTG_HCINTMSK_TXERRM |
					    OTG_HCINTMSK_NAKM |
					    OTG_HCINTMSK_STALLM |
					    OTG_HCINTMSK_CHHM);
			state = Unused;
			current_urb = NULL;
			u->completion(0, u);
			break;
		case CtlSetupTXResult:
			current_urb->endpoint->device.dataToggleOUT = true;
			//the channel disabled itself at this point.
			if (u->buffer_len > 0) {
				if (u->setup.bmRequestType & 0x80) {
					//that's IN
					state = CtlDataRXWait;
					doINTransfer(u->buffer, u->buffer_len);
				} else {
					state = CtlDataTXWait;
					doOUTTransfer(u->buffer, u->buffer_len);
				}
			} else {
				if (u->setup.bmRequestType & 0x80) {
					state = CtlStatusTXWait;
					doOUTTransfer(u->buffer, 0);
				} else {
					state = CtlStatusRXWait;
					doINTransfer(u->buffer, 0);
				}
			}
			break;
		case CtlDataTXResult:
			current_urb->endpoint->device.dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
			//the channel disabled itself at this point.
			state = CtlStatusRXWait;
			doINTransfer(u->buffer, 0);
			break;
		case CtlDataRXWait:
			//the channel disabled itself at this point.
			state = CtlStatusTXWait;
			u->buffer_received = u->buffer_len - data_remaining;
			doOUTTransfer(u->buffer, 0);
			break;
		case CtlStatusTXResult:
			current_urb->endpoint->device.dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
			//fall through
		case CtlStatusRXWait:
			//the channel disabled itself at this point.
			state = Unused;
			current_urb = NULL;
			u->completion(0, u);
			break;
		case RXWait:
			//the channel disabled itself at this point.
			regs->HCINTMSK &= ~(OTG_HCINTMSK_XFRCM |
					    OTG_HCINTMSK_TXERRM |
					    OTG_HCINTMSK_NAKM |
					    OTG_HCINTMSK_STALLM |
					    OTG_HCINTMSK_CHHM);
			state = Unused;
			u->buffer_received = u->buffer_len - data_remaining;
			current_urb = NULL;
			u->completion(0, u);
			break;
		case TXWait: //failed for some reason. keep the chain going.
		case CtlSetupTXWait:
		case CtlDataTXWait:
		case CtlStatusTXWait:
			regs->HCINT = OTG_HCINT_CHH;
			regs->HCCHAR |= OTG_HCCHAR_CHDIS;
			regs->HCINTMSK &= ~(OTG_HCINTMSK_XFRCM |
					    OTG_HCINTMSK_TXERRM |
					    OTG_HCINTMSK_NAKM |
					    OTG_HCINTMSK_STALLM);
			regs->HCINTMSK |= OTG_HCINTMSK_CHHM;
			state = Disabling;
			u->buffer_received = u->buffer_len - data_remaining;
			current_urb = NULL;
			u->completion(1, u);
			break;
		}
	}
	if (regs->HCINT & OTG_HCINT_TXERR) {
		regs->HCINT = OTG_HCINT_TXERR;
		regs->HCINT = OTG_HCINT_CHH;
		regs->HCCHAR |= OTG_HCCHAR_CHDIS;
		regs->HCINTMSK &= ~(OTG_HCINTMSK_XFRCM |
				    OTG_HCINTMSK_TXERRM |
				    OTG_HCINTMSK_NAKM |
				    OTG_HCINTMSK_STALLM);
		regs->HCINTMSK |= OTG_HCINTMSK_CHHM;
		state = Disabling;
		u->buffer_received = u->buffer_len - data_remaining;
		current_urb = NULL;
		u->completion(1, u);
	}
	if (regs->HCINT & OTG_HCINT_STALL) {
		//STALL during any part of a Control transfer means the device
		//is unhappy with it and cannot proceed.

		regs->HCINT = OTG_HCINT_STALL;
		regs->HCINT = OTG_HCINT_CHH;
		regs->HCCHAR |= OTG_HCCHAR_CHDIS;
		regs->HCINTMSK &= ~(OTG_HCINTMSK_XFRCM |
				    OTG_HCINTMSK_TXERRM |
				    OTG_HCINTMSK_NAKM |
				    OTG_HCINTMSK_STALLM);
		regs->HCINTMSK |= OTG_HCINTMSK_CHHM;
		state = Disabling;
		u->buffer_received = u->buffer_len - data_remaining;
		current_urb = NULL;
		u->completion(1, u);
	}
	if (regs->HCINT & OTG_HCINT_NAK) {
		regs->HCINT = OTG_HCINT_NAK;
		//NAK means the device is busy. in TX case, we need to resend
		//the last packet (need more infrastructure for that),
		//in RX case, we "just" need to retrigger sending IN tokens.
		//the data pointer already points to the right place.
		//need to regenerate TX toggle status from register, i think.
		switch(state) {
		case CtlDataRXWait:
			doINTransfer(data, data_remaining);
			return;
		case CtlStatusRXWait:
			doINTransfer(u->buffer, 0);
			return;
		}

		/*
		regs->HCINT = OTG_HCINT_NAK;
		regs->HCINT = OTG_HCINT_CHH;
		regs->HCCHAR |= OTG_HCCHAR_CHDIS;
		regs->HCINTMSK &= ~(OTG_HCINTMSK_XFRCM |
				    OTG_HCINTMSK_TXERRM |
				    OTG_HCINTMSK_NAKM |
				    OTG_HCINTMSK_STALLM);
		regs->HCINTMSK |= OTG_HCINTMSK_CHHM;
		state = Disabling;
		u->buffer_received = u->buffer_len - data_remaining;
		current_urb = NULL;
		u->completion(1, u);
		*/
	}
	if (regs->HCINT & OTG_HCINT_CHH) {
		state = Unused;
		otgh->HAINTMSK &= ~(1 << index);
		regs->HCINT = OTG_HCINT_CHH;
		regs->HCINTMSK &= ~OTG_HCINTMSK_CHHM;
		//and now look at the queue.
		URB *u = NULL;
		{
			ISR_Guard g;
			if (!USB_URBqueue.empty()) {
				u = USB_URBqueue.front();
				USB_URBqueue.pop_front();
			}
		}
		if (u)
			setupForURB(u);
		return;
	}
}

void USBChannel::doSETUPTransfer() {
	uint32_t hcchar = (current_urb->endpoint->device.address << 22)
		| ((current_urb->endpoint->index & 0xf) << 11)
		| current_urb->endpoint->max_packet_length;
	if ((otgh->HFNUM & 1) == 0)
		hcchar |= OTG_HCCHAR_ODDFRM;
	uint32_t hctsiz = 0;
	hctsiz = 0x60000000;
	if (current_urb->endpoint->device.speed == USBSpeed::Low)
		hcchar |= 0x20000;
	hctsiz |= 1 << 19;
	hctsiz |= 8;
	regs->HCCHAR = hcchar;
	regs->HCINTMSK = OTG_HCINTMSK_XFRCM | OTG_HCINTMSK_TXERRM |
		OTG_HCINTMSK_NAKM | OTG_HCINTMSK_STALLM;
	regs->HCTSIZ = hctsiz;
	regs->HCCHAR |= OTG_HCCHAR_CHENA;

	current_urb->endpoint->device.dataToggleIN = true;
	current_urb->endpoint->device.dataToggleOUT = true;
	//now we need to wait for enough space in the non-periodic tx fifo,
	//write the 8 setup bytes and wait for a result.

	data = (uint32_t*)&current_urb->setup;
	data_remaining = 8;

	otgc->GINTMSK |= OTG_GINTMSK_NPTXFEM;
}

void USBChannel::doINTransfer(void *data, size_t xfrsiz) {
	uint32_t hcchar = (current_urb->endpoint->device.address << 22)
		| ((current_urb->endpoint->index & 0xf) << 11)
		| current_urb->endpoint->max_packet_length;
	uint32_t hctsiz = 0;
	switch(current_urb->endpoint->type) {
	case USBEndpoint::Control: hcchar |= 0; break;
	case USBEndpoint::Bulk: hcchar |= 0x80000; break;
	case USBEndpoint::ISO: hcchar |= 0x40000; break;
	case USBEndpoint::IRQ: hcchar |= 0xc0000; break;
	}
	hctsiz = current_urb->endpoint->device.dataToggleIN?0x40000000:0;
	if (current_urb->endpoint->device.speed == USBSpeed::Low)
		hcchar |= 0x20000;
	unsigned pktcnt = xfrsiz / current_urb->endpoint->max_packet_length;
	if ((xfrsiz % current_urb->endpoint->max_packet_length) == 0)
		pktcnt ++;//last packet cannot be max_packet_length, but can be 0.
	hctsiz |= pktcnt << 19;
	hcchar |= 0x8000;
	xfrsiz = pktcnt * current_urb->endpoint->max_packet_length;
	/*
	if (xfrsiz % current_urb->endpoint->max_packet_length) {
		xfrsiz +=
			current_urb->endpoint->max_packet_length -
			(xfrsiz % current_urb->endpoint->max_packet_length);
			}*/
	hctsiz |= xfrsiz;

	this->data = (uint32_t*)data;
	data_remaining = xfrsiz;

	regs->HCCHAR = hcchar;
	regs->HCINTMSK = OTG_HCINTMSK_XFRCM | OTG_HCINTMSK_TXERRM |
		OTG_HCINTMSK_NAKM | OTG_HCINTMSK_STALLM;
	regs->HCTSIZ = hctsiz;
	regs->HCCHAR |= OTG_HCCHAR_CHENA;
	//and now, we can go ahead and stream the data into the fifo.
	//probably need to acquire exclusive access on the fifo first?
	//for control transfers, SETUP, IN/OUT DATA and STATUS phases
	//are seperate transactions.
	//STATUS for IN control transfers is an OUT packet with 0 or more data bytes.
	//STATUS for OUT control transfers is an IN packet with 0 data bytes

	otgc->GINTMSK |= OTG_GINTMSK_RXFLVLM;
}

void USBChannel::doOUTTransfer(void *data, size_t xfrsiz) {
	uint32_t hcchar = (current_urb->endpoint->device.address << 22)
		| ((current_urb->endpoint->index & 0xf) << 11)
		| current_urb->endpoint->max_packet_length;
	uint32_t hctsiz = 0;
	switch(current_urb->endpoint->type) {
	case USBEndpoint::Control: hcchar |= 0; break;
	case USBEndpoint::Bulk: hcchar |= 0x80000; break;
	case USBEndpoint::ISO: hcchar |= 0x40000; break;
	case USBEndpoint::IRQ: hcchar |= 0xc0000; break;
	}
	hctsiz = current_urb->endpoint->device.dataToggleOUT?0x40000000:0;
	if (current_urb->endpoint->device.speed == USBSpeed::Low)
		hcchar |= 0x20000;
	unsigned pktcnt = xfrsiz / current_urb->endpoint->max_packet_length;
	if ((xfrsiz % current_urb->endpoint->max_packet_length) == 0)
		pktcnt ++;//last packet cannot be max_packet_length, but can be 0.
	hctsiz |= pktcnt << 19;
	hctsiz |= xfrsiz;
	regs->HCCHAR = hcchar;
	regs->HCINTMSK = OTG_HCINTMSK_XFRCM | OTG_HCINTMSK_TXERRM |
		OTG_HCINTMSK_NAKM | OTG_HCINTMSK_STALLM;
	regs->HCTSIZ = hctsiz;
	regs->HCCHAR |= OTG_HCCHAR_CHENA;
	//and now, we can go ahead and stream the data into the fifo.
	//probably need to acquire exclusive access on the fifo first?
	//for control transfers, SETUP, IN/OUT DATA and STATUS phases
	//are seperate transactions.
	//STATUS for IN control transfers is an OUT packet with 0 or more data bytes.
	//STATUS for OUT control transfers is an IN packet with 0 data bytes

	this->data = (uint32_t*)data;
	data_remaining = xfrsiz;

	switch(current_urb->endpoint->type) {
	case USBEndpoint::Control:
	case USBEndpoint::Bulk:
		otgc->GINTMSK |= OTG_GINTMSK_NPTXFEM; break;
	case USBEndpoint::ISO:
	case USBEndpoint::IRQ:
		otgc->GINTMSK |= OTG_GINTMSK_PTXFEM; break;
	}
}

void USBChannel::setupForURB(URB *u) {
	//okay, now we are talking. we'll have to remember how much we
	//wrote already, and the odd/even data packet numbers.
	//(SETUP packets reset that one)
	current_urb = u;
	switch(u->endpoint->type) {
	case USBEndpoint::Control:
		state = CtlSetupTXWait;
		doSETUPTransfer();
		break;
	case USBEndpoint::Bulk:
	case USBEndpoint::ISO:
	case USBEndpoint::IRQ:
		if (u->endpoint->direction == USBEndpoint::DeviceToHost) {
			state = RXWait;
			doINTransfer(u->buffer, u->buffer_len);
		} else {
			state = TXWait;
			doOUTTransfer(u->buffer, u->buffer_len);
		}
		break;
	}
	otgh->HAINTMSK |= 1 << index;
}

void USB_submitURB(URB *u) {
	//so, what do we do here?
	//check if we have a free channel. if not => queue it.
	//allocate said channel and prepare it
	//copy data to the right places?
	//trigger channel?
	ISR_Guard g;
	for(auto &ch : channels) {
		if (ch.state == USBChannel::Unused) {
			ch.setupForURB(u);
			return;
		}
	}
	//waiting for a free channel
	USB_URBqueue.push_back(u);
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

static void USB_activationComplete() {
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
