
#pragma once

#include <vector>
#include <string>
#include <deque>

#include <refcounted.hpp>

#include <usb/usb.hpp>
#include <usb/usbproto.h>

struct USBEndpoint;

enum class USBSpeed { Full, Low };

class USBDevice : public Refcounted<USBDevice> {
public:
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
		std::vector<Endpoint> endpoints;
		std::vector<ExtraDescriptor> extraDescriptors;
	};
	class Interface {
	public:
		uint8_t interfaceNumber;
		uint8_t rAlternateSetting;
		uint16_t eAlternateSetting;
		USBDriverDevice *claimed;
		std::vector<AlternateSetting> alternateSettings;
		Interface()
			: rAlternateSetting(0)
			, eAlternateSetting(0xffff)
			, claimed(NULL)
			{}
	};
	class Configuration {
	public:
		USBDescriptorConfiguration descriptor;
		std::vector<ExtraDescriptor> extraDescriptors;
		std::vector<Interface> interfaces;
		Configuration(std::vector<uint8_t> const &descriptors);
	};
private:
	USBSpeed speed;
	uint8_t raddress; ///< real address, used for accounting
	uint8_t eaddress; ///< effective address, used for communication
	std::deque<USBEndpoint*> endpoints;
	friend struct USBChannel; //for speed, eaddress, datatoggleIN/OUT

	enum { None, Address, DescDevice8, DescDevice, Disconnected,
	       FetchManuString, FetchProdString, FetchConfigurations,
	       Unconfigured, Configuring, ConfiguringInterfaces,
	       Configured } state;
	USBDescriptorDevice deviceDescriptor;
	std::string manufacturer;
	std::string product;
	std::vector<uint8_t> descriptordata;
	struct USBDeviceURB {
		USBDevice *_this;
		URB u;
	};
	USBDeviceURB urb;
	std::vector<Configuration> configurations;
	Configuration *rconfiguration; ///< selected configuration
	Configuration *econfiguration; ///< effective configuration

	void urbCompletion(int result, URB *u);
	static void _urbCompletion(int result, URB *u);
	void prepareStringFetch(uint8_t id);
	void prepareConfigurationFetch(uint8_t id);

	void configureInterfaces();
	void updateEndpoints(Interface &intf);
public:
	USBDevice(USBSpeed speed);
	~USBDevice();
	void activate();
	void disconnected();
	RefPtr<USBEndpoint> getEndpoint(uint8_t address);
	std::vector<Configuration> const &getConfigurations() {
		return configurations;
	}
	Configuration const *getSelectedConfiguration() {
		return NULL;
	}
	AlternateSetting const *getAlternateSetting(uint8_t interfaceNumber, uint8_t alternateSetting) {
		if(!econfiguration)
			return NULL;
		auto intfit = econfiguration->interfaces.begin();
		while(intfit->interfaceNumber != interfaceNumber) {
			intfit++;
			if (intfit == econfiguration->interfaces.end())
				return NULL;
		}
		auto altfit = intfit->alternateSettings.begin();
		while(altfit->descriptor.bAlternateSetting != alternateSetting) {
			altfit++;
			if (altfit == intfit->alternateSettings.end())
				return NULL;
		}
		return &*altfit;
	}
	//this is a one-way street: once a configuration has been
	//selected, it stays for life.
	void selectConfiguration(uint8_t bConfigurationValue);
	//claim an interface and alternate setting. if it is already in use,
	//does not claim and returns false
	bool claimInterface(uint8_t bInterfaceNumber,
			    uint8_t bAlternateSetting,
			    USBDriverDevice *dev);
};
