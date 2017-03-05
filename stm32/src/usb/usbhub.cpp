

#include <usb/usb.hpp>
#include <usb/usbdevice.hpp>
#include <usb/usbendpoint.hpp>
#include <vector>
#include <deque>
#include <bits.h>
#include <timer.h>
#include "usbpriv.hpp"

#include "usbhub.h"
#include "usbproto/hub.h"

class USBHUB : public USBDriver {
public:
	virtual bool probe(RefPtr<USBDevice> device);
};

class USBHUBDev : public USBDriverDevice {
private:
	struct Port {
		enum Flags {
			NeedsCheck = 1,
			DeviceRemovable = 2,
			NeedsReset = 4,
			Powered = 8,
			Activating = 16
		};
		uint8_t flags;
		uint8_t num;
		uint16_t status;
		uint16_t change; //pending change bits
		uint32_t resetTimer;
		USBHUBDev *hubdev;
		RefPtr<USBDevice> device;
		static void _resetTimeout(void *data);
		static void _activate(void *data);
		Port() : flags(NeedsCheck),
			 status(0),
			 change(0),
			 resetTimer(0) {}
	};
	RefPtr<USBDevice> device;
	enum { None, FetchHUBDescriptor, CheckingHubStatus,
	       CheckingPortStatus, Configured, PoweringPort,
	       ChangeAck, PortInitReset, PortInitResetAck,
	       PortInitEnable, PortInitFetchStatus
	} state;

	struct USBHUBDeviceURB {
		USBHUBDev *_this;
		URB u;
	} irqurb, ctlurb;
	std::vector<uint8_t> ctldata;
	std::vector<uint8_t> irqdata;
	uint8_t input_endpoint;
	uint8_t input_polling_interval;
	std::vector<Port> ports;
	bool needsHubStatusCheck;
	uint16_t hubStatus;
	void ctlurbCompletion(int result, URB *u);
	static void _ctlurbCompletion(int result, URB *u);
	void irqurbCompletion(int result, URB */*u*/);
	static void _irqurbCompletion(int result, URB *u);

	void checkStatus();
public:
	USBHUBDev(RefPtr<USBDevice> device)
		: device(device)
		{}
	~USBHUBDev() {
	}
	//does nothing since hubs do not support SET INTERFACE
	virtual void deviceClaimed();
	virtual void disconnected(RefPtr<USBDevice> /*device*/);
};

static const uint16_t USBClassHUB = 9;

bool USBHUB::probe(RefPtr<USBDevice> device) {
	//looks like hubs are only allowed to be complete devices without
	//any other functionality. Look for a matching device class.
	if (device->getDeviceDescriptor().bDeviceClass != USBClassHUB)
		return false;
	//found one! claim it, create our
	//USBHUBDev and let that one continue
	//to initialize the device
	USBHUBDev *dev = new USBHUBDev(device);

	for(auto const &conf : device->getConfigurations()) {
		for(auto const &intf : conf.interfaces) {
			for(auto const &altset : intf.alternateSettings) {
				if (altset.descriptor.bInterfaceClass ==
				    USBClassHUB) {
					device->selectConfiguration
						(conf.descriptor.bConfigurationValue);
					if (!device->claimDevice(dev))
						return false;
					return true;
				}
			}
		}
	}
	return false;
}

struct USBHUBStatus {
	uint16_t status;
	uint16_t change;
} PACKED;

#define USBHUB_PORT_CONNECTION (0x1)
#define USBHUB_PORT_POWER (0x100)
#define USBHUB_PORT_LOW_SPEED (0x200)
#define USBHUB_C_PORT_CONNECTION (0x1)
#define USBHUB_C_PORT_ENABLE (0x2)
#define USBHUB_C_PORT_SUSPEND (0x4)
#define USBHUB_C_PORT_OVER_CURRENT (0x8)
#define USBHUB_C_PORT_RESET (0x10)

void USBHUBDev::ctlurbCompletion(int result, URB *u) {
	if (result != 0) {
		//try again
		USB_submitURB(u);
		return;
	}
	switch(state) {
	case FetchHUBDescriptor: {
		//yay, got the descriptor!
		//now, parse it.
		USBDescriptorHUB *d = (USBDescriptorHUB*)ctldata.data();
		if (u->buffer_received < d->bLength) {
			//reissue with the correct length.
			ctlurb.u.setup.wLength = d->bLength;
			ctldata.resize(d->bLength);
			ctlurb.u.buffer = ctldata.data();
			ctlurb.u.buffer_len = ctldata.size();
			USB_submitURB(u);
			return;
		}

		//setup the port infos
		ports.resize(d->bNbrPorts);
		for(unsigned int i = 1; i <= d->bNbrPorts; i++) {
			ports[i-1].num = i;
			ports[i-1].hubdev = this;
			if (d->DeviceRemovable[i >> 3] & (1 << (i & 7)))
				ports[i-1].flags |= Port::DeviceRemovable;
		}

		//setup the URB
		irqurb._this = this;
		irqurb.u.endpoint = device->getEndpoint(input_endpoint);
		assert(irqurb.u.endpoint);
		irqdata.resize((d->bNbrPorts+1+7) >> 3);
		irqurb.u.pollingInterval = input_polling_interval;
		irqurb.u.buffer = irqdata.data();
		irqurb.u.buffer_len = irqdata.size();
		irqurb.u.completion = _irqurbCompletion;

		USB_submitURB(&irqurb.u);

		needsHubStatusCheck = true;
		state = Configured;

		checkStatus();

		break;
	}
	case CheckingHubStatus: {
		USBHUBStatus *s = (USBHUBStatus*)ctldata.data();
		hubStatus = s->status;
		state = Configured;
		//nothing to do with that info, yet.
		//todo: any bits in .change that we have to acknowledge?
		checkStatus();
		break;
	}
	case CheckingPortStatus: {
		USBHUBStatus *s = (USBHUBStatus*)ctldata.data();
		ports[u->setup.wIndex-1].status = s->status;
		ports[u->setup.wIndex-1].change = s->change;
		state = Configured;
		checkStatus();
		break;
	}
	case PoweringPort: // we get informed via irq if there is sth connected
	case ChangeAck:
	{
		state = Configured;
		checkStatus();
		break;
	}
	case PortInitReset:
	{
		state = PortInitResetAck;

		//acknowledge the reset
		u->setup.bmRequestType = 0x23;
		u->setup.bRequest = 1;//CLEAR FEATURE
		u->setup.wValue = 20;//C PORT RESET
		u->setup.wIndex = u->setup.wIndex;
		u->setup.wLength = 0;
		u->buffer = NULL;
		u->buffer_len = 0;

		USB_submitURB(u);

		break;
	}
	case PortInitResetAck:
	{
		state = PortInitEnable;

		//now do the enable.
		u->setup.bmRequestType = 0x23;
		u->setup.bRequest = 3;//SET FEATURE
		u->setup.wValue = 1;//PORT ENABLE
		u->setup.wIndex = u->setup.wIndex;
		u->setup.wLength = 0;
		u->buffer = NULL;
		u->buffer_len = 0;

		USB_submitURB(u);

		break;
	}
	case PortInitEnable: {
		state = PortInitFetchStatus;

		u->setup.bmRequestType = 0xa3;
		u->setup.bRequest = 0;//GET STATUS
		u->setup.wValue = 0;
		u->setup.wLength = 4;
		ctldata.resize(4);
		u->buffer = ctldata.data();
		u->buffer_len = ctldata.size();

		USB_submitURB(u);

		break;
	}
	case PortInitFetchStatus: {
		USBHUBStatus *s = (USBHUBStatus*)ctldata.data();
		ports[u->setup.wIndex-1].status = s->status;
		ports[u->setup.wIndex-1].change = s->change;
		ports[u->setup.wIndex-1].flags &= ~Port::Activating;

		//port is enabled, we know the speed setting.
		//now we need to do the activation with address
		if (s->status & USBHUB_PORT_LOW_SPEED) {
			ports[u->setup.wIndex-1].device =
				new USBDevice(USBSpeed::Low);
		} else {
			ports[u->setup.wIndex-1].device =
				new USBDevice(USBSpeed::Full);
		}
		ports[u->setup.wIndex-1].device->activate();

		state = Configured;
		break;
	}
	default: assert(0); break;
	}
}

void USBHUBDev::_ctlurbCompletion(int result, URB *u) {
	USBHUBDeviceURB *du = container_of(u, USBHUBDeviceURB, u);
	du->_this->ctlurbCompletion(result, u);
}

void USBHUBDev::irqurbCompletion(int result, URB */*u*/) {
	//this is called repeatedly by the usb subsystem.
	if (result != 0)
		return;
	if (irqdata[0] & 1)
		needsHubStatusCheck = true;
	for(unsigned int i = 1; i <= ports.size(); i++) {
		if (irqdata[i >> 3] & (1 << (i & 7)))
			ports[i-1].flags |= Port::NeedsCheck;
	}
	checkStatus();
}

void USBHUBDev::_irqurbCompletion(int result, URB *u) {
	USBHUBDeviceURB *du = container_of(u, USBHUBDeviceURB, u);
	du->_this->irqurbCompletion(result, u);
}

void USBHUBDev::deviceClaimed() {
	//find our endpoint
	auto const *selconf = device->getSelectedConfiguration();
	assert(isRWPtr(selconf));
	for(auto const &intf : selconf->interfaces) {
		for(auto const &altset : intf.alternateSettings) {
			for(auto const &ep : altset.endpoints) {
				if (ep.descriptor.bEndpointAddress & 0x80) {
					input_endpoint = ep.descriptor.bEndpointAddress;
					input_polling_interval = ep.descriptor.bInterval;
				}
			}
		}
	}

	state = FetchHUBDescriptor;
	ctlurb._this = this;
	ctlurb.u.endpoint = device->getEndpoint(0);
	ctlurb.u.setup.bmRequestType = 0xa0;
	ctlurb.u.setup.bRequest = 6;//GET DESCRIPTOR
	ctlurb.u.setup.wValue = (0x29 << 8);
	ctlurb.u.setup.wIndex = 0;
	ctlurb.u.setup.wLength = 7;//go with 7, then reissue if not big enough.
	ctldata.resize(7);
	ctlurb.u.buffer = ctldata.data();
	ctlurb.u.buffer_len = ctldata.size();
	ctlurb.u.completion = _ctlurbCompletion;

	USB_submitURB(&ctlurb.u);
}

void USBHUBDev::disconnected(RefPtr<USBDevice> /*device*/) {
	ctlurb.u.endpoint = NULL;
	USB_retireURB(&irqurb.u);
	irqurb.u.endpoint = NULL;
	for(auto &p : ports) {
		if (p.device) {
			p.device->disconnected();
			p.device = NULL;
		}
	}
}

void USBHUBDev::checkStatus() {
	if (state != Configured)
		return;
	if (needsHubStatusCheck) {
		//DO IT!
		state = CheckingHubStatus;

		ctlurb.u.setup.bmRequestType = 0xa0;
		ctlurb.u.setup.bRequest = 0;//GET STATUS
		ctlurb.u.setup.wValue = 0;
		ctlurb.u.setup.wIndex = 0;
		ctlurb.u.setup.wLength = 4;
		ctldata.resize(4);
		ctlurb.u.buffer = ctldata.data();
		ctlurb.u.buffer_len = ctldata.size();
		ctlurb.u.completion = _ctlurbCompletion;

		needsHubStatusCheck = false;

		USB_submitURB(&ctlurb.u);
		return;
	}
	for(unsigned int i = 0; i < ports.size(); i++) {
		if (ports[i].flags & Port::NeedsCheck) {
			state = CheckingPortStatus;

			ctlurb.u.setup.bmRequestType = 0xa3;
			ctlurb.u.setup.bRequest = 0;//GET STATUS
			ctlurb.u.setup.wValue = 0;
			ctlurb.u.setup.wIndex = i+1;
			ctlurb.u.setup.wLength = 4;
			ctldata.resize(4);
			ctlurb.u.buffer = ctldata.data();
			ctlurb.u.buffer_len = ctldata.size();
			ctlurb.u.completion = _ctlurbCompletion;

			ports[i].flags &= ~Port::NeedsCheck;

			USB_submitURB(&ctlurb.u);

			return;
		}
		if (ports[i].flags & Port::NeedsReset) {
			//on some hubs, the port gets enabled as a result
			//of a port reset, contrary to the spec.
			//so, we need to do the full device setup here, instead
			//of deferring it to the point where we enable the
			//device.
			state = PortInitReset;
			ports[i].flags &= ~Port::NeedsReset;

			ctlurb.u.setup.bmRequestType = 0x23;
			ctlurb.u.setup.bRequest = 3;//SET FEATURE
			ctlurb.u.setup.wValue = 4;//PORT RESET
			ctlurb.u.setup.wIndex = i+1;
			ctlurb.u.setup.wLength = 0;
			ctlurb.u.buffer = NULL;
			ctlurb.u.buffer_len = 0;

			USB_submitURB(&ctlurb.u);
			return;
		}
		//check if the port is powered. if not, we need to do that.
		//todo: do we need a timeout in case we have overcurrent
		//status?
		if (!(ports[i].status & USBHUB_PORT_POWER) &&
		    !(ports[i].flags & Port::Powered)) {
			state = PoweringPort;
			ports[i].flags |= Port::Powered;

			ctlurb.u.setup.bmRequestType = 0x23;
			ctlurb.u.setup.bRequest = 3;//SET FEATURE
			ctlurb.u.setup.wValue = 8;//PORT POWER
			ctlurb.u.setup.wIndex = i+1;
			ctlurb.u.setup.wLength = 0;
			ctlurb.u.buffer = NULL;
			ctlurb.u.buffer_len = 0;

			USB_submitURB(&ctlurb.u);
			return;
		}
		//if there are any .change bits, need to at least acknowledge
		//and possibly flag the action needed for that.

		if (ports[i].change & USBHUB_C_PORT_CONNECTION) {
			if (ports[i].status & USBHUB_PORT_CONNECTION) {
				//need to wait for 100ms, then do a port reset
				ports[i].resetTimer =
					Timer_Oneshot(100000, Port::_resetTimeout, &ports[i]);
			} else {
				Timer_Cancel(ports[i].resetTimer);
				//if we have a device allocated, drop it.
				if (ports[i].flags & Port::Activating) {
					USB_activationComplete();
					ports[i].flags &= ~Port::Activating;
				}
				if (ports[i].device) {
					ports[i].device->disconnected();
					ports[i].device = NULL;
				}
			}
			ports[i].change &= ~USBHUB_C_PORT_CONNECTION;

			state = ChangeAck;
			ctlurb.u.setup.bmRequestType = 0x23;
			ctlurb.u.setup.bRequest = 1;//CLEAR FEATURE
			ctlurb.u.setup.wValue = 16;//C PORT CONNECTION
			ctlurb.u.setup.wIndex = i+1;
			ctlurb.u.setup.wLength = 0;
			ctlurb.u.buffer = NULL;
			ctlurb.u.buffer_len = 0;

			USB_submitURB(&ctlurb.u);

			return;
		}
		if (ports[i].change & USBHUB_C_PORT_ENABLE) {
			//this happens when the device is disabled.
			//todo: how to handle? new reset?
			ports[i].change &= ~USBHUB_C_PORT_ENABLE;

			state = ChangeAck;

			ctlurb.u.setup.bmRequestType = 0x23;
			ctlurb.u.setup.bRequest = 1;//CLEAR FEATURE
			ctlurb.u.setup.wValue = 17;//C PORT ENABLE
			ctlurb.u.setup.wIndex = i+1;
			ctlurb.u.setup.wLength = 0;
			ctlurb.u.buffer = NULL;
			ctlurb.u.buffer_len = 0;

			USB_submitURB(&ctlurb.u);

			return;
		}
		if (ports[i].change & USBHUB_C_PORT_SUSPEND) {
			//this happens when the device has finished resuming
			//todo: how to handle?
			ports[i].change &= ~USBHUB_C_PORT_SUSPEND;

			state = ChangeAck;

			ctlurb.u.setup.bmRequestType = 0x23;
			ctlurb.u.setup.bRequest = 1;//CLEAR FEATURE
			ctlurb.u.setup.wValue = 18;//C PORT SUSPEND
			ctlurb.u.setup.wIndex = i+1;
			ctlurb.u.setup.wLength = 0;
			ctlurb.u.buffer = NULL;
			ctlurb.u.buffer_len = 0;

			USB_submitURB(&ctlurb.u);

			return;
		}
		if (ports[i].change & USBHUB_C_PORT_OVER_CURRENT) {
			//this happens when over current changed, in either
			//direction.
			//todo: how to handle?
			ports[i].change &= ~USBHUB_C_PORT_OVER_CURRENT;

			state = ChangeAck;

			ctlurb.u.setup.bmRequestType = 0x23;
			ctlurb.u.setup.bRequest = 1;//CLEAR FEATURE
			ctlurb.u.setup.wValue = 19;//C PORT OVER CURRENT
			ctlurb.u.setup.wIndex = i+1;
			ctlurb.u.setup.wLength = 0;
			ctlurb.u.buffer = NULL;
			ctlurb.u.buffer_len = 0;

			USB_submitURB(&ctlurb.u);

			return;
		}
		if (ports[i].change & USBHUB_C_PORT_RESET) {
			//this happens when the port reset ends.
			//now is a good time to begin the activation process.
			//todo: how to handle?
			ports[i].change &= ~USBHUB_C_PORT_RESET;

			state = ChangeAck;

			ctlurb.u.setup.bmRequestType = 0x23;
			ctlurb.u.setup.bRequest = 1;//CLEAR FEATURE
			ctlurb.u.setup.wValue = 20;//C PORT RESET
			ctlurb.u.setup.wIndex = i+1;
			ctlurb.u.setup.wLength = 0;
			ctlurb.u.buffer = NULL;
			ctlurb.u.buffer_len = 0;

			USB_submitURB(&ctlurb.u);

			return;
		}

		//todo: we probably want to react on changes, like
		//PORT_CONNECTION(0)(connect/disconnect, also check status),
		//PORT_RESET(4)(when reset completed)
		//for PORT_CONNECTION, we probably better off comparing agains
		//our own version.
	}
}

void USBHUBDev::Port::_resetTimeout(void*data) {
	//just flag it, then ask the hub device about anything it wants to do.
	USB_queueDeviceActivation(_activate, data);
}

void USBHUBDev::Port::_activate(void*data) {
	USBHUBDev::Port *_this = (USBHUBDev::Port*)data;
	_this->flags |= NeedsReset | Activating;
	_this->hubdev->checkStatus();
}

static USBHUB usbhub_driver;

void USBHUB_Setup() {
	USB_registerDriver(&usbhub_driver);
}
