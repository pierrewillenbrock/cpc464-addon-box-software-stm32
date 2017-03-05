
#include <usb/usbdevice.hpp>

#include <usb/usbendpoint.hpp>
#include "usbpriv.hpp"
#include <bsp/stm32f4xx.h>
#include <lang.hpp>
#include <bits.h>
#include <irq.h>
#include <eventlogger.hpp>

#include <algorithm>
#include <string.h>

static OTG_Core_TypeDef * const otgc = OTGF_CORE;
static OTG_Host_TypeDef * const otgh = OTGF_HOST;

USBDevice::USBDevice(USBSpeed speed)
	: speed(speed)
	, raddress(0)
	, eaddress(0)
	, state(None)
	, rconfiguration(NULL)
	, econfiguration(NULL)
	, claimed(NULL)
{
	USBEndpoint *ne = new USBEndpoint(*this);
	endpoints.push_back(ne);
	assert(isRWPtr(ne));
	ne->direction = USBEndpoint::HostToDevice;
	ne->type = USBEndpoint::Control;
	ne->address = 0;
	ne->max_packet_length = 8;
}

USBDevice::~USBDevice() {
	if (state == Address)
		USB_activationComplete();
	USB_deactivateAddress(eaddress);
	for(auto ep : endpoints) {
		USB_killEndpoint(ep);
		delete ep;
	}
}

USBDevice::Configuration::Configuration(std::vector<uint8_t> const &descriptors) {
	uint8_t const *dp = descriptors.data();
	uint8_t const *de = dp + descriptors.size();
	memcpy(&descriptor, dp, sizeof(descriptor));
	dp += descriptor.bLength;
	AlternateSetting *currentAlternateSetting;
	Endpoint *currentEndpoint;
	enum { InConfiguration, InInterface, InEndpoint }
	state = InConfiguration;
	while(dp < de) {
		if (dp[1] == 4) {
			//Interface
			USBDescriptorInterface const *d =
				(USBDescriptorInterface *)dp;
			//see if we can find the interface
			auto iit = std::find_if(interfaces.begin(),
						interfaces.end(),
						[d](auto &i) {
							assert(isRWPtr(d));
							return i.interfaceNumber == d->bInterfaceNumber;
						});
			if (iit == interfaces.end()) {
				//create one
				interfaces.push_back(Interface());
				iit = interfaces.end();
				iit--;
				iit->interfaceNumber = d->bInterfaceNumber;
			}
			//this is a new alternate setting.
			AlternateSetting as;
			memcpy(&as.descriptor,d, sizeof(USBDescriptorInterface));
			iit->alternateSettings.push_back(as);
			currentAlternateSetting = &iit->alternateSettings.back();
			currentEndpoint = NULL;

			state = InInterface;
		} else if (dp[1] == 5) {
			//Endpoint
			USBDescriptorEndpoint const *d =
				(USBDescriptorEndpoint *)dp;

			Endpoint ep;
			memcpy(&ep.descriptor,d, sizeof(USBDescriptorEndpoint));
			assert(isRWPtr(currentAlternateSetting));
			currentAlternateSetting->endpoints.push_back(ep);
			currentEndpoint = &currentAlternateSetting->endpoints.back();
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
			case InInterface: {
				assert(isRWPtr(currentAlternateSetting));
				currentAlternateSetting->
					extraDescriptors.push_back(e);
				break;
			}
			case InEndpoint: {
				assert(isRWPtr(currentEndpoint));
				currentEndpoint->
					extraDescriptors.push_back(e);
				break;
			}
			}
		}
		dp += dp[0];//bLength
	}
}

void USBDevice::prepareStringFetch(uint8_t id) {

	urb.u.setup.bmRequestType = 0x80;
	urb.u.setup.bRequest = 6;
	urb.u.setup.wValue = (3 << 8) | id;//STRING descriptor type, index id
	urb.u.setup.wIndex = 0x409;//the right thing to do is to pull id 0, take any langid we like, then default to 0x409. but windows pretty much requires to have 0x409 in first position there, so we could go for that exclusively as well.
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
	assert(isRWPtr(u));
	if (result != 0) {
		if (state == DescDevice8) {
			LogEvent("USBDevice: Resend, DescDevice8");
			//we need to try all the sizes, some devices just
			//emit STALL if we get it too small.
			u->setup.wLength *= 2;
			if (u->setup.wLength > 64)
				u->setup.wLength = 8;
			endpoints[0]->max_packet_length = u->setup.wLength;
			descriptordata.resize(u->setup.wLength);
			u->buffer = descriptordata.data();
			u->buffer_len = descriptordata.size();
		} else {
			LogEvent("USBDevice: Resend");
		}
		//try again
		USB_submitURB(u);
		return;
	}
	switch(state) {
	case Address: {
		LogEvent("USBDevice: Address");
		//now that the address has been set on the device, we need
		//to update our effective address
		eaddress = u->setup.wValue;
		//address has been handled, can activate another device.
		USB_activationComplete();
		//now the device is in "Address" mode. need to fetch the
		//configurations and let a driver select one. then we go to "Configured"
		u->setup.bmRequestType = 0x80;
		u->setup.bRequest = 6;//GET DESCRIPTOR
		u->setup.wValue = (1 << 8) | 0;//DEVICE descriptor type, index 0
		u->setup.wIndex = 0;//no language
		u->setup.wLength = 8; //for now, just pull 8. need to increase this for the full descriptor.
		descriptordata.resize(u->setup.wLength);
		u->buffer = descriptordata.data();
		u->buffer_len = descriptordata.size();

		state = DescDevice8;
		USB_submitURB(u);
		break;
	}
	case DescDevice8: {
		LogEvent("USBDevice: DescDevice8");
		USBDescriptorDevice *d = (USBDescriptorDevice*)
			descriptordata.data();
		assert(isRWPtr(d));
		endpoints[0]->max_packet_length = d->bMaxPacketSize0;

		if (u->buffer_received < d->bLength) {
			u->setup.wLength = d->bLength;
			descriptordata.resize(d->bLength);
			u->buffer = descriptordata.data();
			u->buffer_len = descriptordata.size();

			state = DescDevice;
			USB_submitURB(u);
			break;
		} else {
			//we have all the data needed
			state = DescDevice;
			//and fall through.
		}
	}
	case DescDevice:
		LogEvent("USBDevice: DescDevice");
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
			state = Unconfigured;
			descriptordata.clear();
			descriptordata.shrink_to_fit();
			urb.u.endpoint = NULL;
			USB_registerDevice(this);
		}
		break;
	case FetchManuString: {
		LogEvent("USBDevice: FetchManuString");
		USBDescriptorString *d = (USBDescriptorString *)
			descriptordata.data();
		assert(isRWPtr(d));
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
			state = Unconfigured;
			descriptordata.clear();
			descriptordata.shrink_to_fit();
			urb.u.endpoint = NULL;
			USB_registerDevice(this);
		}
		break;
	}
	case FetchProdString: {
		LogEvent("USBDevice: FetchProdString");
		USBDescriptorString *d = (USBDescriptorString *)
			descriptordata.data();
		assert(isRWPtr(d));
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
			state = Unconfigured;
			descriptordata.clear();
			descriptordata.shrink_to_fit();
			urb.u.endpoint = NULL;
			USB_registerDevice(this);
		}
		break;
	}
	case FetchConfigurations: {
		LogEvent("USBDevice: FetchConfigurations");
		USBDescriptorConfiguration *d = (USBDescriptorConfiguration *)
			descriptordata.data();
		assert(isRWPtr(d));
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
			state = Unconfigured;
			descriptordata.clear();
			descriptordata.shrink_to_fit();
			urb.u.endpoint = NULL;
			USB_registerDevice(this);
		}
		break;
	}
	case Configuring: {
		LogEvent("USBDevice: Configuring");
		econfiguration = rconfiguration;

		for(auto &intf : econfiguration->interfaces) {
			intf.eAlternateSetting =
				intf.alternateSettings.front().descriptor.bAlternateSetting;
			updateEndpoints(intf);

			if(intf.claimed &&
			   intf.eAlternateSetting == intf.rAlternateSetting) {
				assert(isRWPtr(intf.claimed));
				intf.claimed->interfaceClaimed
					(intf.interfaceNumber,
					 intf.rAlternateSetting);
			}
		}

		if (claimed)
			claimed->deviceClaimed();
		urb.u.endpoint = NULL;
		ISR_Guard g;
		state = Configured;
		configureInterfaces();
		break;
	}
	case ConfiguringInterfaces: {
		LogEvent("USBDevice: ConfiguringInterfaces");
		urb.u.endpoint = NULL;
		ISR_Guard g;

		assert(isRWPtr(econfiguration));
		auto iit = std::find_if(econfiguration->interfaces.begin(),
				     econfiguration->interfaces.end(),
				     [u](auto &i){
						assert(isRWPtr(u));
						return i.interfaceNumber == u->setup.wIndex;
				     });
		if (iit == econfiguration->interfaces.end()) {
			assert(0);
		}
		auto &intf = *iit;

		intf.eAlternateSetting = intf.rAlternateSetting;
		updateEndpoints(intf);
		assert(isRWPtr(intf.claimed));
		intf.claimed->interfaceClaimed
		  (u->setup.wIndex, intf.rAlternateSetting);

		state = Configured;
		configureInterfaces();
		break;
	}
	default: assert(0); break;
	}
}

void USBDevice::_urbCompletion(int result, URB *u) {
	USBDeviceURB *du = container_of(u, USBDeviceURB, u);
	assert(isRWPtr(du));
	assert(isRWPtr(du->_this));
	du->_this->urbCompletion(result, u);
}

void USBDevice::activate() {
	//device now is in "Default" state.
	//give it an address.
	//then prepare an URB to switch to that address
	raddress = USB_getNextAddress();
	eaddress = 0; //default address
	state = Address;
	urb._this = this;
	urb.u.endpoint = endpoints[0];
	urb.u.setup.bmRequestType = 0x00;
	urb.u.setup.bRequest = 5;//SET ADDRESS
	urb.u.setup.wValue = raddress;
	urb.u.setup.wIndex = 0;
	urb.u.setup.wLength = 0;
	urb.u.buffer = NULL;
	urb.u.buffer_len = 0;
	urb.u.completion = _urbCompletion;

	USB_submitURB(&urb.u);
}

void USBDevice::disconnected() {
	//also trigger all that is needed to drop all the references to us
	//if that is not done otherwise.
	if (state == Address)
		USB_activationComplete();
	state = Disconnected;
	USB_unregisterDevice(this);

	if (rconfiguration) {
		assert(isRWPtr(rconfiguration));
		for(auto &intf : rconfiguration->interfaces) {
			if (intf.claimed) {
				assert(isRWPtr(intf.claimed));
				intf.claimed->disconnected(this);
			}
		}
		if (claimed) {
			assert(isRWPtr(claimed));
			claimed->disconnected(this);
		}
	}

	USB_deactivateAddress(eaddress);

	ISR_Guard g;
	for(auto ep : endpoints) {
		USB_killEndpoint(ep);
		delete ep;
	}
	endpoints.clear();
}

RefPtr<USBEndpoint> USBDevice::getEndpoint(uint8_t address) {
	auto it = std::find_if(endpoints.begin(), endpoints.end(),
			       [address](auto &e){
				       assert(isRWPtr(e));
				       return e->address == address;
			       });

	if (it != endpoints.end())
		return *it;
	return NULL;
}

void USBDevice::selectConfiguration(uint8_t bConfigurationValue) {
	//if no configuration is selected, the device must essentially be idle.
	ISR_Guard g;
	assert(state == Unconfigured);//we can support changing during Configured, but then all Interfaces must be unclaimed.
	rconfiguration = NULL;
	for(auto &c : configurations) {
		if (c.descriptor.bConfigurationValue == bConfigurationValue)
			rconfiguration = &c;
	}
	if (!rconfiguration)
		return;
	state = Configuring;

	urb.u.endpoint = endpoints[0];
	urb.u.setup.bmRequestType = 0x00;
	urb.u.setup.bRequest = 9;//SET CONFIGURATION
	urb.u.setup.wValue = bConfigurationValue;
	urb.u.setup.wIndex = 0;
	urb.u.setup.wLength = 0;
	urb.u.buffer = NULL;
	urb.u.buffer_len = 0;
	urb.u.completion = _urbCompletion;

	USB_submitURB(&urb.u);
}

void USBDevice::configureInterfaces() {
	if (!econfiguration || state != Configured)
		return;

	assert(isRWPtr(econfiguration));
	for(auto &intf : econfiguration->interfaces) {
		if (intf.rAlternateSetting != intf.eAlternateSetting &&
		    intf.claimed) {
			if (intf.alternateSettings.size() > 1) {
				state = ConfiguringInterfaces;

				urb.u.endpoint = endpoints[0];
				urb.u.setup.bmRequestType = 0x01;
				urb.u.setup.bRequest = 11;//SET INTERFACE
				urb.u.setup.wValue = intf.rAlternateSetting;
				urb.u.setup.wIndex = intf.interfaceNumber;
				urb.u.setup.wLength = 0;
				urb.u.buffer = NULL;
				urb.u.buffer_len = 0;
				urb.u.completion = _urbCompletion;

				USB_submitURB(&urb.u);
				return;
			} else {
				intf.eAlternateSetting =
					intf.rAlternateSetting;
				assert(isRWPtr(intf.claimed));
				intf.claimed->interfaceClaimed
				  (intf.interfaceNumber,
				   intf.rAlternateSetting);
			}
		}
	}
}

bool USBDevice::claimInterface(uint8_t bInterfaceNumber,
			       uint8_t bAlternateSetting,
			       USBDriverDevice *dev) {
	ISR_Guard g;
	if (!rconfiguration)
		return false;
	if (claimed)
		return false;
	assert(isRWPtr(rconfiguration));
	auto iit = std::find_if(rconfiguration->interfaces.begin(),
				rconfiguration->interfaces.end(),
				[bInterfaceNumber](auto &i){
					return i.interfaceNumber == bInterfaceNumber;
				});
	if (iit == rconfiguration->interfaces.end())
		return false;
	auto &intf = *iit;
	if (intf.claimed)
		return false;
	auto ait = std::find_if(intf.alternateSettings.begin(),
				intf.alternateSettings.end(),
				[bAlternateSetting](auto &a) {
					return a.descriptor.bAlternateSetting == bAlternateSetting;
				});
	if (ait == intf.alternateSettings.end())
		return false;
	intf.rAlternateSetting = bAlternateSetting;
	intf.claimed = dev;
	configureInterfaces();

	return true;
}

bool USBDevice::claimDevice(USBDriverDevice *dev) {
	ISR_Guard g;
	if (!rconfiguration)
		return false;
	if (claimed)
		return false;
	assert(isRWPtr(rconfiguration));
	for(auto &intf : rconfiguration->interfaces) {
		if (intf.claimed)
			return false;
	}
	claimed = dev;

	if (state == Configured)
		dev->deviceClaimed();

	return true;
}

void USBDevice::updateEndpoints(Interface &intf) {
	for(auto &altset : intf.alternateSettings) {
		for(auto &ep : altset.endpoints) {
			auto eit = std::find_if(endpoints.begin(), endpoints.end(),
						[ep](auto &e) {
							assert(isRWPtr(e));
							return e->address ==
							ep.descriptor.bEndpointAddress;
						});
			if (eit != endpoints.end()) {
				USBEndpoint * uep = *eit;
				endpoints.erase(eit);
				USB_killEndpoint(uep);
				delete uep;
			}
		}
	}
	auto ait = std::find_if(intf.alternateSettings.begin(),
				intf.alternateSettings.end(),
				[intf](auto &a) {
					return a.descriptor.bAlternateSetting ==
					intf.eAlternateSetting;
				});
	assert(ait != intf.alternateSettings.end());
	auto &altset = *ait;
	for(auto &ep : altset.endpoints) {
		USBEndpoint *ne = new USBEndpoint(*this);;
		assert(isRWPtr(ne));
		endpoints.push_back(ne);
		ne->direction =
			(ep.descriptor.bEndpointAddress & 0x80)?
			USBEndpoint::DeviceToHost:USBEndpoint::HostToDevice;
		ne->dataToggleIN = false;
		ne->dataToggleOUT = false;
		switch(ep.descriptor.bmAttributes & 0x03) {
		case 0x00:
			ne->type = USBEndpoint::Control;
			break;
		case 0x01:
			ne->type = USBEndpoint::ISO;
			break;
		case 0x02:
			ne->type = USBEndpoint::Bulk;
			break;
		case 0x03:
			ne->type = USBEndpoint::IRQ;
			break;
		}
		ne->address = ep.descriptor.bEndpointAddress;
		ne->max_packet_length = ep.descriptor.wMaxPacketSize;
		//bInterval is passed to the core by the driver through the urb
	}
}
