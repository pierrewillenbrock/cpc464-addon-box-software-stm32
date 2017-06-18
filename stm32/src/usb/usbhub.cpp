

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

class USBHUB : public usb::Driver
 {
public:
	virtual bool probe(RefPtr<usb::Device> device);
};

class USBHUBDev : public usb::DriverDevice
 {
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
		RefPtr<usb::Device> device;
		static void _resetTimeout(void *data);
		static void _activate(void *data);
		Port() : flags(NeedsCheck),
			 status(0),
			 change(0),
			 resetTimer(0) {}
	};
	RefPtr<usb::Device> device;
	enum { None, FetchHUBDescriptor, CheckingHubStatus,
	       CheckingPortStatus, Configured, PoweringPort,
	       ChangeAck, PortInitReset, PortInitResetAck,
	       PortInitFetchStatus, Disconnected
	} state;

	usb::URB irqurb, ctlurb;
	std::vector<uint8_t> ctldata;
	std::vector<uint8_t> irqdata;
	uint8_t input_endpoint;
	uint8_t input_polling_interval;
	std::vector<Port> ports;
	bool needsHubStatusCheck;
	uint16_t hubStatus;
	uint8_t activatingPort;
	void ctlurbCompletion(int result, usb::URB *u);
	void irqurbCompletion(int result, usb::URB */*u*/);

	void checkStatus();
public:
	USBHUBDev(RefPtr<usb::Device> device)
		: device(device)
		{}
	virtual ~USBHUBDev() {
	}
	//does nothing since hubs do not support SET INTERFACE
	virtual void deviceClaimed();
	virtual void disconnected(RefPtr<usb::Device> /*device*/);
};

static const uint16_t USBClassHUB = 9;

bool USBHUB::probe(RefPtr<usb::Device> device) {
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

void USBHUBDev::ctlurbCompletion(int result, usb::URB *u) {
	if (result != 0) {
		//try again
		usb::submitURB(u);
		return;
	}
	switch(state) {
	case FetchHUBDescriptor: {
		//yay, got the descriptor!
		//now, parse it.
		USBDescriptorHUB *d = (USBDescriptorHUB*)ctldata.data();
		if (u->buffer_received < d->bLength) {
			//reissue with the correct length.
			ctlurb.setup.wLength = d->bLength;
			ctldata.resize(d->bLength);
			ctlurb.buffer = ctldata.data();
			ctlurb.buffer_len = ctldata.size();
			usb::submitURB(u);
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
		irqurb.endpoint = device->getEndpoint(input_endpoint);
		assert(irqurb.endpoint);
		irqdata.resize((d->bNbrPorts+1+7) >> 3);
		irqurb.pollingInterval = input_polling_interval;
		irqurb.buffer = irqdata.data();
		irqurb.buffer_len = irqdata.size();
		irqurb.slot = sigc::mem_fun(this, &USBHUBDev::irqurbCompletion);

		usb::submitURB(&irqurb);

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
		state = Configured;
		break;
	}
	case PortInitResetAck:
	{
		state = PortInitFetchStatus;

		u->setup.bmRequestType = 0xa3;
		u->setup.bRequest = 0;//GET STATUS
		u->setup.wValue = 0;
		u->setup.wLength = 4;
		ctldata.resize(4);
		u->buffer = ctldata.data();
		u->buffer_len = ctldata.size();

		usb::submitURB(u);

		break;
	}
	case PortInitFetchStatus: {
		USBHUBStatus *s = (USBHUBStatus*)ctldata.data();
		ports[u->setup.wIndex-1].status = s->status;
		ports[u->setup.wIndex-1].change = s->change;
		ports[u->setup.wIndex-1].flags &= ~Port::Activating;

		//port is enabled, we know the speed setting.
		//now we need to do the activation with address
		if (s->status & USBHUB_PORT_STATUS_PORT_LOW_SPEED) {
			ports[u->setup.wIndex-1].device =
				new usb::Device (usb::Speed::Low);
		} else {
			ports[u->setup.wIndex-1].device =
				new usb::Device (usb::Speed::Full);
		}
		ports[u->setup.wIndex-1].device->activate();
		activatingPort = 0xff;

		state = Configured;
		break;
	}
	default: assert(0); break;
	}
}

void USBHUBDev::irqurbCompletion(int result, usb::URB */*u*/) {
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
	activatingPort = 0xff;
	ctlurb.endpoint = device->getEndpoint(0);
	ctlurb.setup.bmRequestType = 0xa0;
	ctlurb.setup.bRequest = 6;//GET DESCRIPTOR
	ctlurb.setup.wValue = (0x29 << 8);
	ctlurb.setup.wIndex = 0;
	ctlurb.setup.wLength = 7;//go with 7, then reissue if not big enough.
	ctldata.resize(7);
	ctlurb.buffer = ctldata.data();
	ctlurb.buffer_len = ctldata.size();
	ctlurb.slot = sigc::mem_fun(this, &USBHUBDev::ctlurbCompletion);

	usb::submitURB(&ctlurb);
}

void USBHUBDev::disconnected(RefPtr<usb::Device> /*device*/) {
	state = Disconnected;
	usb::retireURB(&ctlurb);
	ctlurb.endpoint = NULL;
	usb::retireURB(&irqurb);
	irqurb.endpoint = NULL;
	for(auto &p : ports) {
		if (p.device) {
			p.device->disconnected();
			p.device = NULL;
		}
	}
	delete this;
}

void USBHUBDev::checkStatus() {
	if (state != Configured)
		return;
	if (needsHubStatusCheck) {
		state = CheckingHubStatus;

		ctlurb.setup.bmRequestType = 0xa0;
		ctlurb.setup.bRequest = 0;//GET STATUS
		ctlurb.setup.wValue = 0;
		ctlurb.setup.wIndex = 0;
		ctlurb.setup.wLength = 4;
		ctldata.resize(4);
		ctlurb.buffer = ctldata.data();
		ctlurb.buffer_len = ctldata.size();
		ctlurb.slot = sigc::mem_fun(this, &USBHUBDev::ctlurbCompletion);

		needsHubStatusCheck = false;

		usb::submitURB(&ctlurb);
		return;
	}
	for(unsigned int i = 0; i < ports.size(); i++) {
		if (ports[i].flags & Port::NeedsCheck) {
			state = CheckingPortStatus;

			ctlurb.setup.bmRequestType = 0xa3;
			ctlurb.setup.bRequest = 0;//GET STATUS
			ctlurb.setup.wValue = 0;
			ctlurb.setup.wIndex = i+1;
			ctlurb.setup.wLength = 4;
			ctldata.resize(4);
			ctlurb.buffer = ctldata.data();
			ctlurb.buffer_len = ctldata.size();
			ctlurb.slot = sigc::mem_fun(this, &USBHUBDev::ctlurbCompletion);

			ports[i].flags &= ~Port::NeedsCheck;

			usb::submitURB(&ctlurb);

			return;
		}
		if ((ports[i].flags & Port::NeedsReset) && activatingPort == 0xff) {
			//on some hubs, the port gets enabled as a result
			//of a port reset, contrary to the spec.
			//so, we need to do the full device setup here, instead
			//of deferring it to the point where we enable the
			//device.
			state = PortInitReset;
			activatingPort = i;
			ports[i].flags &= ~Port::NeedsReset;

			ctlurb.setup.bmRequestType = 0x23;
			ctlurb.setup.bRequest = USB_REQUEST_SET_FEATURE;
			ctlurb.setup.wValue = USBHUB_FEATURE_PORT_RESET;
			ctlurb.setup.wIndex = i+1;
			ctlurb.setup.wLength = 0;
			ctlurb.buffer = NULL;
			ctlurb.buffer_len = 0;

			usb::submitURB(&ctlurb);
			return;
		}
		//check if the port is powered. if not, we need to do that.
		//todo: do we need a timeout in case we have overcurrent
		//status?
		if (!(ports[i].status & USBHUB_PORT_STATUS_PORT_POWER) &&
		    !(ports[i].flags & Port::Powered)) {
			state = PoweringPort;
			ports[i].flags |= Port::Powered;

			ctlurb.setup.bmRequestType = 0x23;
			ctlurb.setup.bRequest = USB_REQUEST_SET_FEATURE;
			ctlurb.setup.wValue = USBHUB_FEATURE_PORT_POWER;
			ctlurb.setup.wIndex = i+1;
			ctlurb.setup.wLength = 0;
			ctlurb.buffer = NULL;
			ctlurb.buffer_len = 0;

			usb::submitURB(&ctlurb);
			return;
		}
		//if there are any .change bits, need to at least acknowledge
		//and possibly flag the action needed for that.

		if (ports[i].change & USBHUB_PORT_STATUS_C_PORT_CONNECTION) {
			if (ports[i].status & USBHUB_PORT_STATUS_PORT_CONNECTION) {
				//need to wait for 100ms, then do a port reset
				ports[i].resetTimer =
					Timer_Oneshot(100000, Port::_resetTimeout, &ports[i]);
			} else {
				Timer_Cancel(ports[i].resetTimer);
				//if we have a device allocated, drop it.
				if (ports[i].flags & Port::Activating) {
					usb::activationComplete();
					ports[i].flags &= ~Port::Activating;
				}
				if (ports[i].device) {
					ports[i].device->disconnected();
					ports[i].device = NULL;
				}
			}
			ports[i].change &= ~USBHUB_PORT_STATUS_C_PORT_CONNECTION;

			state = ChangeAck;
			ctlurb.setup.bmRequestType = 0x23;
			ctlurb.setup.bRequest = USB_REQUEST_CLEAR_FEATURE;
			ctlurb.setup.wValue = USBHUB_FEATURE_C_PORT_CONNECTION;
			ctlurb.setup.wIndex = i+1;
			ctlurb.setup.wLength = 0;
			ctlurb.buffer = NULL;
			ctlurb.buffer_len = 0;

			usb::submitURB(&ctlurb);

			return;
		}
		if (ports[i].change & USBHUB_PORT_STATUS_C_PORT_ENABLE) {
			//this happens when the device is disabled.
			//todo: how to handle? new reset?
			ports[i].change &= ~USBHUB_PORT_STATUS_C_PORT_ENABLE;

			state = ChangeAck;

			ctlurb.setup.bmRequestType = 0x23;
			ctlurb.setup.bRequest = USB_REQUEST_CLEAR_FEATURE;
			ctlurb.setup.wValue = USBHUB_FEATURE_C_PORT_ENABLE;
			ctlurb.setup.wIndex = i+1;
			ctlurb.setup.wLength = 0;
			ctlurb.buffer = NULL;
			ctlurb.buffer_len = 0;

			usb::submitURB(&ctlurb);

			return;
		}
		if (ports[i].change & USBHUB_PORT_STATUS_C_PORT_SUSPEND) {
			//this happens when the device has finished resuming
			//todo: how to handle?
			ports[i].change &= ~USBHUB_PORT_STATUS_C_PORT_SUSPEND;

			state = ChangeAck;

			ctlurb.setup.bmRequestType = 0x23;
			ctlurb.setup.bRequest = USB_REQUEST_CLEAR_FEATURE;
			ctlurb.setup.wValue = USBHUB_FEATURE_C_PORT_SUSPEND;
			ctlurb.setup.wIndex = i+1;
			ctlurb.setup.wLength = 0;
			ctlurb.buffer = NULL;
			ctlurb.buffer_len = 0;

			usb::submitURB(&ctlurb);

			return;
		}
		if (ports[i].change & USBHUB_PORT_STATUS_C_PORT_OVER_CURRENT) {
			//this happens when over current changed, in either
			//direction.
			//todo: how to handle?
			ports[i].change &= ~USBHUB_PORT_STATUS_C_PORT_OVER_CURRENT;

			state = ChangeAck;

			ctlurb.setup.bmRequestType = 0x23;
			ctlurb.setup.bRequest = USB_REQUEST_CLEAR_FEATURE;
			ctlurb.setup.wValue = USBHUB_FEATURE_C_PORT_OVER_CURRENT;
			ctlurb.setup.wIndex = i+1;
			ctlurb.setup.wLength = 0;
			ctlurb.buffer = NULL;
			ctlurb.buffer_len = 0;

			usb::submitURB(&ctlurb);

			return;
		}
		if (ports[i].change & USBHUB_PORT_STATUS_C_PORT_RESET) {
			//this happens when the port reset ends.
			//now is a good time to begin the activation process.
			//todo: how to handle?
			ports[i].change &= ~USBHUB_PORT_STATUS_C_PORT_RESET;

			assert(ports[i].flags & Port::Activating);
			assert(activatingPort == i);
			state = PortInitResetAck;

			ctlurb.setup.bmRequestType = 0x23;
			ctlurb.setup.bRequest = USB_REQUEST_CLEAR_FEATURE;
			ctlurb.setup.wValue = USBHUB_FEATURE_C_PORT_RESET;
			ctlurb.setup.wIndex = i+1;
			ctlurb.setup.wLength = 0;
			ctlurb.buffer = NULL;
			ctlurb.buffer_len = 0;

			usb::submitURB(&ctlurb);

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
	usb::queueDeviceActivation (_activate, data);
}

void USBHUBDev::Port::_activate(void*data) {
	USBHUBDev::Port *_this = (USBHUBDev::Port*)data;
	_this->flags |= NeedsReset | Activating;
	assert(_this->hubdev);
	_this->hubdev->checkStatus();
}

static USBHUB usbhub_driver;

void USBHUB_Setup() {
	usb::registerDriver(&usbhub_driver);
}
