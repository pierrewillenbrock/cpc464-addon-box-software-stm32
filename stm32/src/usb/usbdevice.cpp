
#include "usbdevice.hpp"

#include "usbendpoint.hpp"
#include "usbpriv.hpp"
#include <bsp/stm32f4xx.h>
#include <lang.hpp>
#include <bits.h>
#include <irq.h>

#include <string.h>

static OTG_Core_TypeDef * const otgc = OTGF_CORE;
static OTG_Host_TypeDef * const otgh = OTGF_HOST;

USBDevice::USBDevice(USBSpeed speed)
	: state(None)
	, speed(speed) {
	USBEndpoint *endpoint0 = new USBEndpoint(*this);
	endpoint0->direction = USBEndpoint::HostToDevice;
	endpoint0->type = USBEndpoint::Control;
	endpoint0->index = 0;
	endpoint0->max_packet_length = 8;
	endpoints.push_back(endpoint0);
}

USBDevice::~USBDevice() {
	for(auto ep : endpoints) {
		delete ep;
	}
}

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
		USB_activationComplete(address);
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
		urb.u.setup.wValue = USB_getNextAddress();
	}
	urb.u.setup.wIndex = 0;
	urb.u.setup.wLength = 0;
	urb.u.buffer = NULL;
	urb.u.buffer_len = 0;
	urb.u.completion = _urbCompletion;

	USB_submitURB(&urb.u);
}

