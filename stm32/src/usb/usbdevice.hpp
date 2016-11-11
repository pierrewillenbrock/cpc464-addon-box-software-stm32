
#pragma once

#include <vector>
#include <string>
#include <unordered_map>

#include <usb/usb.h>
#include "usbproto.h"

struct USBEndpoint;

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
	USBDevice(USBSpeed speed);
	~USBDevice();
	void activate();
	void urbCompletion(int result, URB *u);
	static void _urbCompletion(int result, URB *u);
	void prepareStringFetch(uint8_t id);
	void prepareConfigurationFetch(uint8_t id);
};
