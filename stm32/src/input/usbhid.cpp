

#include <input/usbhid.h>
#include <usb/usb.hpp>
#include <usb/usbdevice.hpp>
#include <usb/usbendpoint.hpp>
#include <vector>
#include <deque>
#include <bits.h>
#include <input/input.hpp>
#include <sstream>
#include <iomanip>

#include <usbproto/hid.h>

#define SUPPORT_INPUTS
#undef SUPPORT_OUTPUTS
#undef SUPPORT_FEATURES

#undef SUPPORT_STRINGS
#undef SUPPORT_DESIGNATORS

/** \brief Structures to support USB HID devices
 */
namespace usbhid {

	struct UsageInfo {
		uint32_t usage_min;
		uint32_t usage_max;
		std::vector<uint32_t> usages;
		uint32_t getUsage(unsigned idx) {
			if (!usages.empty()) {
				if (idx >= usages.size())
					return 0;
				return usages[idx];
			} else {
				if (idx >= usage_max - usage_min)
					return 0;
				return idx + usage_min;
			}
		}
	};

#ifdef SUPPORT_STRINGS
	struct StringInfo {
		uint8_t string_min;
		uint8_t string_max;
		std::deque<uint8_t> strings;
		StringInfo getString() {
			uint8_t s;
			if (!strings.empty()) {
				s = strings.front();
				if (strings.size() > 1)
					strings.pop_front();
			} else {
				s = string_min;
				if (string_min < string_max)
					string_min++;
			}
			StringInfo i;
			i.string_min = s;
			i.string_max = s;
			return i;
		}
		void clear() {
			string_min = 0;
			string_max = 0;
			strings.clear();
		}
	};
#endif

#ifdef SUPPORT_DESIGNATORS
	struct DesignatorInfo {
		uint8_t designator_min;
		uint8_t designator_max;
		std::deque<uint8_t> designators;
		DesignatorInfo getDesignator() {
			uint8_t d;
			if (!designators.empty()) {
				d = designators.front();
				if (designators.size() > 1)
					designators.pop_front();
			} else {
				d = designator_min;
				if (designator_min < designator_max)
					designator_min++;
			}
			DesignatorInfo i;
			i.designator_min = d;
			i.designator_max = d;
			return i;
		}
		void clear() {
			designator_min = 0;
			designator_max = 0;
			designators.clear();
		}
	};
#endif

	struct GlobalState {
		uint16_t usage_page;
		uint8_t report_id;
		uint8_t report_size;
		uint8_t report_count;
		int32_t logical_minimum;
		int32_t logical_maximum;
		int32_t physical_minimum;
		int32_t physical_maximum;
		int32_t unit_exponent;
		uint32_t unit;
		GlobalState() {
			usage_page = 0;
			report_id = 0;
			report_size = 0;
			report_count = 1;
			logical_minimum = 0;
			logical_maximum = 0;
			physical_minimum = 0;
			physical_maximum = 0;
			unit_exponent = 0;
			unit = 0;
		}
	};

	struct LocalState {
		unsigned delimiter;
		std::vector<UsageInfo> usages;
#ifdef SUPPORT_DESIGNATORS
		DesignatorInfo designators;
#endif
#ifdef SUPPORT_STRINGS
		StringInfo strings;
#endif
		void reset() {
			delimiter = ~0U;
			usages.clear();
#ifdef SUPPORT_DESIGNATORS
			designators.clear();
#endif
#ifdef SUPPORT_STRINGS
			strings.clear();
#endif
		}
	};

#if defined(SUPPORT_INPUTS) || defined(SUPPORT_OUTPUTS) || defined(SUPPORT_FEATURES)
	class Report;

	struct InOut {
		uint32_t flags;
		std::vector<UsageInfo> usages;
#ifdef SUPPORT_DESIGNATORS
		DesignatorInfo designators;
#endif
#ifdef SUPPORT_STRINGS
		StringInfo strings;
#endif
		int32_t logical_minimum;
		int32_t logical_maximum;
		int32_t physical_minimum;
		int32_t physical_maximum;
		int32_t unit_exponent;
		uint32_t unit;
		uint8_t report_count;
		uint16_t control_info_index;
		Report *report;
		std::vector<int32_t> last_values;
	};
#endif

	class Collection { // this is a hierarchical view of the device
	public:
		uint32_t type;
		uint32_t usage;
		std::vector<Collection> collections;
#ifdef SUPPORT_INPUTS
		std::vector<InOut*> inputs;
#endif
#ifdef SUPPORT_OUTPUTS
		std::vector<InOut*> outputs;
#endif
#ifdef SUPPORT_FEATURES
		std::vector<InOut*> features;
#endif
		Collection() : type(0xff), usage(0) {}
		Collection(uint32_t type, uint32_t usage) : type(type), usage(usage) {}
		void clear() {
			collections.clear();
#ifdef SUPPORT_INPUTS
			inputs.clear();
#endif
#ifdef SUPPORT_OUTPUTS
			outputs.clear();
#endif
#ifdef SUPPORT_FEATURES
			features.clear();
#endif
		}
	};

#if defined(SUPPORT_INPUTS) || defined(SUPPORT_OUTPUTS) || defined(SUPPORT_FEATURES)
	class Report { // this is a decoding/encoding view of the reports
	public:
		uint8_t report_id;//using 0 to denote "report id system not used".
		enum Flags {
			Requested = 0x01,
			Received = 0x02
		};
		uint32_t flags;
		Report(uint8_t report_id) : report_id(report_id) {}
		struct Element {
			uint8_t report_size;//in bits
			uint8_t report_count;//overall size covered by this Element is report_size * report_count
			uint16_t report_position;//in bits. uint8_t would probably suffice, but the next 8 bits would be padding in any case.
			InOut *inout;
		};
		std::vector<Element> elements;
	};
#endif
}

using namespace usbhid;

class USBHID : public usb::Driver
 {
public:
	virtual bool probe(RefPtr<usb::Device> device);
};

class USBHIDDev : public usb::DriverDevice, public input::Device {
private:
	RefPtr<usb::Device> device;
	enum { None, FetchReportDescriptor, InitialReportRequests, Configured, Disconnected
	} state;

	struct USBHIDDeviceURB {
		USBHIDDev *_this;
		usb::URB u;
	} irqurb, ctlurb;
	std::vector<uint8_t> ctldata;
	std::vector<uint8_t> irqdata;
	USBHIDDescriptorHID *hiddescriptor;
	uint8_t input_endpoint;
	uint8_t input_polling_interval;
	uint8_t interfaceNumber;
	void ctlurbCompletion(int result, usb::URB *u);
	static void _ctlurbCompletion(int result, usb::URB *u);
#ifdef SUPPORT_INPUTS
	void irqurbCompletion(int result, usb::URB */*u*/);
	static void _irqurbCompletion(int result, usb::URB *u);
#endif
	void parseReportDescriptor(uint8_t *data, size_t size);

#ifdef SUPPORT_INPUTS
	std::vector<Report*> inputreports;
#endif
#ifdef SUPPORT_OUTPUTS
	std::vector<Report*> outputreports;
#endif
#ifdef SUPPORT_FEATURES
	std::vector<Report*> featurereports;
#endif
#if defined(SUPPORT_INPUTS) || defined(SUPPORT_OUTPUTS) || defined(SUPPORT_FEATURES)
	std::vector<InOut*> inouts;
#endif
	Collection deviceCollection;

#if defined(SUPPORT_INPUTS) || defined(SUPPORT_OUTPUTS) || defined(SUPPORT_FEATURES)
	void setValues(InOut *inout, std::vector<int32_t> &values);
#endif
#if defined(SUPPORT_INPUTS) || defined(SUPPORT_FEATURES)
	void parseReport(std::vector<Report*> const &reports, std::vector<uint8_t> const &urbdata);
#endif
public:
	USBHIDDev(RefPtr<usb::Device> device)
		: device(device)
		{}
	virtual ~USBHIDDev() {
#ifdef SUPPORT_INPUTS
		for(auto &r : inputreports)
			delete r;
#endif
#ifdef SUPPORT_OUTPUTS
		for(auto &r : outputreports)
			delete r;
#endif
#ifdef SUPPORT_FEATURES
		for(auto &r : featurereports)
			delete r;
#endif
#if defined(SUPPORT_INPUTS) || defined(SUPPORT_OUTPUTS) || defined(SUPPORT_FEATURES)
		for(auto &i : inouts)
			delete i;
#endif
	}
	virtual void interfaceClaimed(uint8_t interfaceNumber, uint8_t alternateSetting);
	virtual void disconnected(RefPtr<usb::Device> /*device*/);
	virtual std::vector<input::Report> getCurrentInputReports() const;
	virtual uint32_t countryCode() const;
	std::string name() const;
};

static const uint16_t USBClassHID = 3;

bool USBHID::probe(RefPtr<usb::Device> device) {
	//we need to find a HID class Interface in the configurations
	auto const *selconf = device->getSelectedConfiguration();
	if (!selconf) {
		for(auto const &conf : device->getConfigurations()) {
			for(auto const &intf : conf.interfaces) {
				for(auto const &altset : intf.alternateSettings) {
					if (altset.descriptor.bInterfaceClass ==
					    USBClassHID) {
						//found one! claim it, create our
						//USBHIDDev and let that one continue
						//to initialize the device
						USBHIDDev *dev = new USBHIDDev
							(device);

						device->selectConfiguration
							(conf.descriptor.bConfigurationValue);
						device->claimInterface
							(altset.descriptor.bInterfaceNumber,
							 altset.descriptor.bAlternateSetting,
							 dev);
						return true;
					}
				}
			}
		}
	} else {
		for(auto const &intf : selconf->interfaces) {
			for(auto const &altset : intf.alternateSettings) {
				if (altset.descriptor.bInterfaceClass ==
				    USBClassHID) {
					//found one! claim it, create our
					//USBHIDDev and let that one continue
					//to initialize the device
					USBHIDDev *dev = new USBHIDDev(device);
					if (device->claimInterface
					    (altset.descriptor.bInterfaceNumber,
					     altset.descriptor.bAlternateSetting,
					     dev))
						return true;
				}
			}
		}
	}
	return false;
}

void USBHIDDev::parseReportDescriptor(uint8_t *data, size_t size) {
	//essentially, the report descriptor is a sequence of "commands",
	//each modifying state of the parser, creating objects or moving around
	//in the resulting hierarchy

	deviceCollection.type = 0xff;
	deviceCollection.usage = 0;
	deviceCollection.clear();
#ifdef SUPPORT_INPUTS
	for(auto &r : inputreports)
		delete r;
	inputreports.clear();
#endif
#ifdef SUPPORT_OUTPUTS
	for(auto &r : outputreports)
		delete r;
	outputreports.clear();
#endif
#ifdef SUPPORT_FEATURES
	for(auto &r : featurereports)
		delete r;
	featurereports.clear();
#endif
#if defined(SUPPORT_INPUTS) || defined(SUPPORT_OUTPUTS) || defined(SUPPORT_FEATURES)
	for(auto &i : inouts)
		delete i;
	inouts.clear();
#endif


	std::vector<Collection*> collectionstack;
	std::vector<GlobalState> globals;
	globals.resize(1);
	LocalState locals;

	collectionstack.push_back(&deviceCollection);
#ifdef SUPPORT_INPUTS
	Report *inputreport = NULL;
#endif
#ifdef SUPPORT_OUTPUTS
	Report *outputreport = NULL;
#endif
#ifdef SUPPORT_FEATURES
	Report *featurereport = NULL;
#endif

	uint8_t *de = data + size;
	while(data < de) {
		uint8_t opcode = data[0];
		unsigned dataSize;
		if (opcode == 0xfe) {
			//long item
			dataSize = data[1];
			unsigned longItemTag = data[2];
			data += 3;
			switch(longItemTag) {
			default: break; //there are no long item tags defined.
			}
		} else {
			//short item
			switch(opcode & 0x03) {
			case 0:	dataSize = 0; break;
			case 1:	dataSize = 1; break;
			case 2:	dataSize = 2; break;
			case 3:	dataSize = 4; break;
			}
			unsigned tagType = opcode & 0xfc;
			data ++;

			//parse the argument as a signed number
			int32_t argument = 0;
			if (dataSize == 1) {
				argument = (int8_t)(data[0]);
			} else if (dataSize == 2) {
				argument = (int16_t)((data[1] << 8) | data[0]);
			} else if (dataSize == 4) {
				argument = (data[3] << 24) |
				(data[2] << 16) |
				(data[1] << 8) |
				data[0];
			}

			switch(tagType) {
				//Main Items
#ifndef SUPPORT_INPUTS
				case 0x80: // Input
#endif
#ifndef SUPPORT_OUTPUTS
				case 0x90: // Output
#endif
#ifndef SUPPORT_FEATURES
				case 0xb0: // Feature
#endif
#if !defined(SUPPORT_INPUTS) || !defined(SUPPORT_OUTPUTS) || !defined(SUPPORT_FEATURES)
					locals.reset();
					break;
#endif
#ifdef SUPPORT_INPUTS
				case 0x80: // Input
#endif
#ifdef SUPPORT_OUTPUTS
				case 0x90: // Output
#endif
#ifdef SUPPORT_FEATURES
				case 0xb0: // Feature
#endif
#if defined(SUPPORT_INPUTS) || defined(SUPPORT_OUTPUTS) || defined(SUPPORT_FEATURES)
				{
					//this works for flags == 0.
					//array items may need a different representation
					std::vector<InOut*> &iovec =
#if defined(SUPPORT_INPUTS)
#if defined(SUPPORT_OUTPUTS) || defined(SUPPORT_FEATURES)
					(tagType == 0x80)?collectionstack.back()->inputs:
#else
					collectionstack.back()->inputs;
#endif
#endif
#if defined(SUPPORT_OUTPUTS)
#if defined(SUPPORT_FEATURES)
					(tagType == 0x90)?collectionstack.back()->outputs:
#else
					collectionstack.back()->outputs;
#endif
#endif
#if defined(SUPPORT_FEATURES)
					collectionstack.back()->features;
#endif
					Report *& repptr =
#if defined(SUPPORT_INPUTS)
#if defined(SUPPORT_OUTPUTS) || defined(SUPPORT_FEATURES)
					(tagType == 0x80)?inputreport:
#else
					inputreport;
#endif
#endif
#if defined(SUPPORT_OUTPUTS)
#if defined(SUPPORT_FEATURES)
					(tagType == 0x90)?outputreport:
#else
					outputreport;
#endif
#endif
#if defined(SUPPORT_FEATURES)
					featurereport;
#endif
					std::vector<Report*> & repvec =
#if defined(SUPPORT_INPUTS)
#if defined(SUPPORT_OUTPUTS) || defined(SUPPORT_FEATURES)
					(tagType == 0x80)?inputreports:
#else
					inputreports;
#endif
#endif
#if defined(SUPPORT_OUTPUTS)
#if defined(SUPPORT_FEATURES)
					(tagType == 0x90)?outputreports:
#else
					outputreports;
#endif
#endif
#if defined(SUPPORT_FEATURES)
					featurereports;
#endif
					InOut *item = new InOut();
					item->control_info_index = inouts.size();
					inouts.push_back(item);
					iovec.push_back(item);
					item->flags = argument;
					item->usages = locals.usages;
#ifdef SUPPORT_DESIGNATORS
					item->designators = locals.designators;
#endif
#ifdef SUPPORT_STRINGS
					item->strings = locals.strings;
#endif
					item->logical_minimum = globals.back().logical_minimum;
					item->logical_maximum = globals.back().logical_maximum;
					item->physical_minimum =
					globals.back().physical_minimum != 0?
					globals.back().physical_minimum :
					globals.back().logical_minimum;
					item->physical_maximum =
					globals.back().physical_maximum != 0?
					globals.back().physical_maximum :
					globals.back().logical_maximum;
					item->unit_exponent = globals.back().unit_exponent;
					item->unit = globals.back().unit;
					item->report_count = globals.back().report_count;

					if (!repptr) {
						//no report id or anything needing a
						//report seen. create a new one.
						repptr = new Report(globals.back().report_id);
						repptr->flags = 0;
						repvec.push_back(repptr);
					}
					item->report = repptr;
					Report::Element e;
					e.report_size = globals.back().report_size;
					e.report_count = globals.back().report_count;
					if (repptr->elements.empty()) {
						e.report_position = 0;
					} else {
						e.report_position =
						repptr->elements.back().report_position +
						repptr->elements.back().report_size*
						repptr->elements.back().report_count;
					}
					e.inout = item;
					repptr->elements.push_back(e);
					locals.reset();
					break;
				}
#endif
				case 0xa0: // Collection
					collectionstack.back()->collections.push_back(
						Collection(argument,locals.usages.back().getUsage(0)));
					//this pointer will not change since the collection vector containing the
					//collection object is only worked on after this pointer has been removed
					//from the end of collectionstack.
					collectionstack.push_back
					(&collectionstack.back()->collections.back());
					locals.reset();
					break;
				case 0xc0: // End of Collection
					collectionstack.pop_back();
					break;
				//Global Items  (modify the global state table)
				case 0x04: //Usage Page
					globals.back().usage_page = argument;
					break;
				case 0x14: //Logical Minimum
					globals.back().logical_minimum = argument;
					break;
				case 0x24: //Logical Maximum
					globals.back().logical_maximum = argument;
					break;
				case 0x34: //Physical Minimum (these start out at 0, meaning same as logical)
					globals.back().physical_minimum = argument;
					break;
				case 0x44: //Physical Maximum (these start out at 0, meaning same as logical)
					globals.back().physical_maximum = argument;
					break;
				case 0x54: //Unit Exponent (in base 10)
					globals.back().unit_exponent = argument;
					break;
				case 0x64: //Unit
					/* each nibble describes the exponent of a particular
					 * base unit, in 2s complement.
					 * except nibble 0, which is the measuring system.
					 * nibble \ system   0      1            2            3            4
					 *               | None | SI Linear  | SI         | English    | English
					 *               |      |            | Rotation   | Linear     | Rotation
					 * 1 Length      | -    | Centimeter | Radians    | Inch       | Degrees
					 * 2 Mass        | -    | Gram       | Gram       | Slug       | Slug
					 * 3 Time        | -    | Seconds    | Seconds    | Seconds    | Seconds
					 * 4 Temperature | -    | Kelvin     | Kelvin     | Fahrenheit | Fahrenheit
					 * 5 Current     | -    | Ampere     | Ampere     | Ampere     | Ampere
					 * 6 Luminous    | -    | Candela    | Candela    | Candela    | Candela
					 *   Intensity   |      |            |            |            |
					 * 7 Reserved    |      |            |            |            |
					 *
					 * 0x1001, 0x1002, 0x1003, 0x1004 => Seconds
					 * 0x11 => Centimeters
					 * 0xe121 => g*cm*cm/s/s => 1e-7 kg*m*m/s/s => 1e-7 Joule
					 */
					globals.back().unit = argument;
					break;
				case 0x74: //Report Size
					globals.back().report_size = argument;
					break;
				case 0x84: //Report ID
					globals.back().report_id = argument;
					//see if we can find a matching report in each of the
					//report types. the main items create a new one if
					//there is none, yet.
#ifdef SUPPORT_INPUTS
					inputreport = NULL;
					for(auto &r : inputreports) {
						if (r->report_id == argument)
							inputreport = r;
					}
#endif
#ifdef SUPPORT_OUTPUTS
					outputreport = NULL;
					for(auto &r : outputreports) {
						if (r->report_id == argument)
							outputreport = r;
					}
#endif
#ifdef SUPPORT_FEATURES
					featurereport = NULL;
					for(auto &r : featurereports) {
						if (r->report_id == argument)
							featurereport = r;
					}
#endif
					break;
				case 0x94: //Report Count
					globals.back().report_count = argument;
					break;
				case 0xa4: //Push (pushes the global state table)
					globals.push_back(globals.back());
					break;
				case 0xb4: //Pop (pops the global state table)
					globals.pop_back();
					break;
				// Local Items
				case 0x08: //Usage  (with dataSize = 4, the page is encoded in the high 16 bits)
					if (locals.usages.empty()) {
						locals.usages.resize(1);
					}
					if (dataSize < 4)
						argument |= (globals.back().usage_page << 16);
					locals.usages.back().usages.push_back(argument);
					break;
				case 0x18: //Usage Minimum
					if (locals.usages.empty()) {
						locals.usages.resize(1);
					}
					if (dataSize < 4)
						argument |= (globals.back().usage_page << 16);
					locals.usages.back().usage_min = argument;
					break;
				case 0x28: //Usage Maximum
					if (locals.usages.empty()) {
						locals.usages.resize(1);
					}
					if (dataSize < 4)
						argument |= (globals.back().usage_page << 16);
					locals.usages.back().usage_max = argument;
					break;
				case 0x38: //Designator Index
#ifdef SUPPORT_DESIGNATORS
					locals.designators.designators.push_back(argument);
#endif
					break;
				case 0x48: //Designator Minimum
#ifdef SUPPORT_DESIGNATORS
					locals.designators.designator_min = argument;
#endif
					break;
				case 0x58: //Designator Maximum
#ifdef SUPPORT_DESIGNATORS
					locals.designators.designator_max = argument;
#endif
					break;
				case 0x78: //String Index
#ifdef SUPPORT_STRINGS
					locals.strings.strings.push_back(argument);
#endif
					break;
				case 0x88: //String Minimum
#ifdef SUPPORT_STRINGS
					locals.strings.string_min = argument;
#endif
					break;
				case 0x98: //String Maximum
#ifdef SUPPORT_STRINGS
					locals.strings.string_max = argument;
#endif
					break;
				case 0xa8: //Delimiter (used to define sets of alternative usages)
					if (argument == 1)
						locals.usages.push_back(UsageInfo());
			}
		}

		//need to create three groups of reports: input, output, feature
		//all with or without report id. can use report id 0 to indicate
		//no use of the report id system.

		data += dataSize;
	}
}

void USBHIDDev::ctlurbCompletion(int result, usb::URB *u) {
	if (result != 0 && state != InitialReportRequests) {
		//try again
		usb::submitURB(u);
		return;
	}
	switch(state) {
	case FetchReportDescriptor: {
		//yay, got the report!
		//now, parse it.
		parseReportDescriptor(ctldata.data(),u->buffer_received);

		Report *requestReport = NULL;
		bool requestFeature = false;
#ifdef SUPPORT_INPUTS
		//okay, got possibly a number of reports. find the max size of
		//input.
		if (!inputreports.empty()) {
			size_t input_max_size = 0;
			for(auto & ir : inputreports) {
				size_t sz =
				ir->elements.back().report_position +
				ir->elements.back().report_size *
				ir->elements.back().report_count;
				if (input_max_size < sz)
					input_max_size = sz;
				if (requestReport == NULL)
					requestReport = ir;
			}
			input_max_size = (input_max_size + 7)/8;
			if (inputreports.size() > 1 ||
				inputreports.front()->report_id != 0)
				input_max_size += 1;//need space for the report id

			//setup the URB
			irqurb._this = this;
			irqurb.u.endpoint = device->getEndpoint(input_endpoint);
			irqdata.resize(input_max_size);
			irqurb.u.pollingInterval = input_polling_interval;
			irqurb.u.buffer = irqdata.data();
			irqurb.u.buffer_len = irqdata.size();
			irqurb.u.completion = _irqurbCompletion;

			usb::submitURB(&irqurb.u);
		}
#endif
#ifdef SUPPORT_FEATURES
		if (!featurereports.empty()) {
			for(auto & fr : featurereports) {
				if (requestReport == NULL) {
					requestReport = fr;
					requestFeature = true;
				}
			}
		}
#endif
/** \todo Should do a few more things here:
 * => query the current state of each report using GET_REPORT (rt: 0xa1, r: 0x01, v: 0x0100 | reportid, i: interface, l: report length) this may fail on some devices not following the HID specs.
 * the other two are probably not needed:
 * => use SET_PROTOCOL to set report based SET_PROTOCOL(rt: 0x21, r: 0x0b, v: 1, i: interface, l: 0). this request may fail(on non-boot devices).
 * => maybe have to quirk around the gamepad using SET_IDLE( rt: 0x21, r: 0x0a, v: ((duration/4ms) << 8) | reportid, i: interface, l: 0)
 */

		if (requestReport != NULL) {
			size_t sz =
			requestReport->elements.back().report_position +
			requestReport->elements.back().report_size *
			requestReport->elements.back().report_count;
			sz = (sz+7)/8;
			if(!requestFeature) {
#ifdef SUPPORT_INPUTS
				if (inputreports.size() > 1 ||
					inputreports.front()->report_id != 0)
					sz += 1;//need space for the report id
#endif
			} else {
#ifdef SUPPORT_FEATURES
				if (featurereports.size() > 1 ||
					featurereports.front()->report_id != 0)
					sz += 1;//need space for the report id
#endif
			}
			requestReport->flags |= Report::Requested;

			ctlurb.u.setup.bmRequestType = 0xa1;
			ctlurb.u.setup.bRequest = 0x01; //Get_Report
			ctlurb.u.setup.wValue = (requestFeature?0x0300:0x0100) | requestReport->report_id;
			ctlurb.u.setup.wIndex = interfaceNumber;
			ctlurb.u.setup.wLength = sz;
			ctldata.resize(sz);
			ctlurb.u.buffer = ctldata.data();
			ctlurb.u.buffer_len = ctldata.size();

			usb::submitURB(&ctlurb.u);

			state = InitialReportRequests;
		} else
			state = Configured;

		input::AddDevice (this);

		break;
	}
	case InitialReportRequests: {
		if (result == 0) {
			//okay, got an initial report. give it to the parser.
			//todo: factor that one out.
#ifdef SUPPORT_INPUTS
			if ((ctlurb.u.setup.wValue & 0xff00) == 0x0100)
				parseReport(inputreports, ctldata);
#endif
#ifdef SUPPORT_FEATURES
			if ((ctlurb.u.setup.wValue & 0xff00) == 0x0300)
				parseReport(featurereports, ctldata);
#endif
#ifdef SUPPORT_FEATURES
		} else if ((ctlurb.u.setup.wValue & 0xff00) == 0x0300) {
			//rerequest it. for the inputs, it may be reasonable to
			//not respond(and deliver data through the irq instead),
			//but for the feature reports, there is no other way.
			USB_submitURB(u);
			return;
#endif
		}

		//find another report to poll
		Report *requestReport = NULL;
		bool requestFeature = false;
#ifdef SUPPORT_INPUTS
		if (!inputreports.empty()) {
			for(auto & ir : inputreports) {
				if (requestReport == NULL && !(ir->flags & Report::Requested))
					requestReport = ir;
			}
		}
#endif
#ifdef SUPPORT_FEATURES
		if (!featurereports.empty()) {
			for(auto & fr : featurereports) {
				if (requestReport == NULL && !(fr->flags & Report::Requested)) {
					requestReport = fr;
					requestFeature = true;
				}
			}
		}
#endif

		if (requestReport != NULL) {
			size_t sz =
			requestReport->elements.back().report_position +
			requestReport->elements.back().report_size *
			requestReport->elements.back().report_count;
			if(!requestFeature) {
#ifdef SUPPORT_INPUTS
				if (inputreports.size() > 1 ||
					inputreports.front()->report_id != 0)
					sz += 1;//need space for the report id
#endif
			} else {
#ifdef SUPPORT_FEATURES
				if (featurereports.size() > 1 ||
					featurereports.front()->report_id != 0)
					sz += 1;//need space for the report id
#endif
			}
			requestReport->flags |= Report::Requested;

			ctlurb.u.setup.bmRequestType = 0xa1;
			ctlurb.u.setup.bRequest = 0x01;
			ctlurb.u.setup.wValue = (requestFeature?0x0300:0x0100) | requestReport->report_id;
			ctlurb.u.setup.wIndex = interfaceNumber;
			ctlurb.u.setup.wLength = sz;
			ctldata.resize(sz);
			ctlurb.u.buffer = ctldata.data();
			ctlurb.u.buffer_len = ctldata.size();

			usb::submitURB(&ctlurb.u);

			state = InitialReportRequests;
		} else
			state = Configured;

		break;
	}
	default: assert(0); break;
	}
}

void USBHIDDev::_ctlurbCompletion(int result, usb::URB *u) {
	USBHIDDeviceURB *du = container_of(u, USBHIDDeviceURB, u);
	du->_this->ctlurbCompletion(result, u);
}

#if defined(SUPPORT_INPUTS) || defined(SUPPORT_FEATURES)
void USBHIDDev::parseReport(std::vector<Report*> const &reports, std::vector<uint8_t> const &urbdata) {
	Report *rep = reports[0];
	uint8_t const *data = urbdata.data();
	if (reports.size() > 1 ||
	    reports[0]->report_id != 0) {
		rep = NULL;
		for(auto &r : reports) {
			if (r->report_id == data[0])
				rep = r;
		}
		if (!rep)
			return;
		data++;
	}
	rep->flags |= Report::Received;
	std::vector<int32_t> values;
	for(auto &e : rep->elements) {
		if (e.inout->usages.empty())
			continue;
		values.clear();
		for(unsigned i = 0; i < e.report_count; i++) {
			uint32_t v = 0;
			unsigned pos = e.report_position + e.report_size*i;
			size_t size = e.report_size;
			uint32_t dp = data[pos / 8];
			switch((size+7)/8) {
			case 4: dp |= data[pos / 8+3] << 24;
			case 3: dp |= data[pos / 8+2] << 16;
			case 2: dp |= data[pos / 8+1] << 8;
			}
			pos %= 8;
			v = (dp >> pos);
			uint32_t m = (1 << size);
			v &= m -1;
			// If Logical Minimum and Logical Maximum are both
			// positive values then a sign bit is unnecessary in
			// the report field and the contents of a field can be
			// assumed to be an unsigned value. Otherwise, all
			// integer values are signed values represented in
			// 2's complement format.
			// Device Class Defintion for Human Interface Devices
			// (HID) Version 1.1, 5.8
			if((e.inout->logical_minimum < 0 ||
			                e.inout->logical_maximum < 0) &&
			                (v & (m>>1)))
				v -= m;//extend sign
			values.push_back(v);
		}
		setValues(e.inout,values);
	}
}
#endif

#ifdef SUPPORT_INPUTS
void USBHIDDev::irqurbCompletion(int result, usb::URB */*u*/) {
	//this is called repeatedly by the usb subsystem.
	if (result != 0)
		return;
	//cool, let's look at the report.
	if (inputreports.empty())
		return;
	parseReport(inputreports, irqdata);
}

void USBHIDDev::_irqurbCompletion(int result, usb::URB *u) {
	USBHIDDeviceURB *du = container_of(u, USBHIDDeviceURB, u);
	du->_this->irqurbCompletion(result, u);
}

void USBHIDDev::setValues(InOut *inout, std::vector<int32_t> &values) {
	input::Report hir;
	//set some good defaults
	hir.device = this;
	hir.flags = 0;
	if (inout->flags & USBHID_MAINFLAG_RELATIVE)
		hir.flags |= 0x1;
	if (inout->flags & USBHID_MAINFLAG_WRAP)
		hir.flags |= 0x2;
	if (inout->flags & USBHID_MAINFLAG_NON_LINEAR)
		hir.flags |= 0x4;
	if (inout->flags & USBHID_MAINFLAG_NO_PREFERRED)
		hir.flags |= 0x8;
	if (inout->flags & USBHID_MAINFLAG_NULL_STATE)
		hir.flags |= 0x10;
	hir.logical_maximum = inout->logical_maximum;
	hir.logical_minimum = inout->logical_minimum;
	hir.physical_maximum = inout->physical_maximum;
	hir.physical_minimum = inout->physical_minimum;
	hir.unit = inout->unit;
	hir.unit_exponent = inout->unit_exponent;
	if (inout->flags & USBHID_MAINFLAG_VARIABLE) {
		for(unsigned i = 0; i < values.size(); i++) {
			if (i < inout->last_values.size() &&
			    inout->last_values[i] == values[i])
				continue;//did not change
			hir.usage = inout->usages[0].getUsage(i);
			hir.value = values[i];
			reportInput(hir);
		}
	} else {
		//check if we can find the valid last_values
		//in values. if not, report deassertion.
		for(auto &lv : inout->last_values) {
			if (lv < inout->logical_minimum ||
			    lv > inout->logical_maximum)
				continue;
			bool found = false;
			for(auto &v : values) {
				if (lv == v) {
					found = true;
					break;
				}
			}
			if (!found) {
				hir.usage = inout->usages[0].getUsage
					(lv-inout->logical_minimum);
				hir.value = 0;
				reportInput(hir);
			}
		}
		//now look for the inverse:
		for(auto &v : values) {
			if (v < inout->logical_minimum ||
			    v > inout->logical_maximum)
				continue;
			bool found = false;
			for(auto &lv : inout->last_values) {
				if (lv == v) {
					found = true;
					break;
				}
			}
			if (!found) {
				hir.usage = inout->usages[0].getUsage
					(v-inout->logical_minimum);
				hir.value = 1;
				reportInput(hir);
			}
		}
	}
	inout->last_values = values;
}
#endif

void USBHIDDev::interfaceClaimed(uint8_t interfaceNumber, uint8_t alternateSetting) {
	this->interfaceNumber = interfaceNumber;
	//we have available:
	//* the deviceDescriptor(pretty uninteresting)
	//* the configurationDescriptor and interfaceDescriptor(containing the extra HID descriptors)
	//* the endpoints to talk to
	auto const *alt = device->getAlternateSetting(interfaceNumber, alternateSetting);
	//the HID Descriptor must appear between the Interface and any Endpoints
	//so no point in looking into the endpoints.
	for(auto const &ed : alt->extraDescriptors) {
		if (ed.descriptor[1] == 0x21) {
			//HID Descriptor
			hiddescriptor = ((USBHIDDescriptorHID*)ed.descriptor.data());
		}
	}
	for(auto const &ep : alt->endpoints) {
		for(auto const &ed : ep.extraDescriptors) {
			if (ed.descriptor[1] == 0x21) {
				//HID Descriptor
				hiddescriptor = ((USBHIDDescriptorHID*)ed.descriptor.data());
			}
		}
		if (ep.descriptor.bEndpointAddress & 0x80) {
			input_endpoint = ep.descriptor.bEndpointAddress;
			input_polling_interval = ep.descriptor.bInterval;
		}
	}

	//look through the descriptors referenced in hiddescriptor
	//to find report descriptors

	size_t report_desc_size = 0;
	for(unsigned int i = 0; i < hiddescriptor->bNumDescriptors; i++) {
		if (hiddescriptor->subDescriptors[i].bDescriptorType == 0x22) {
			report_desc_size =
				hiddescriptor->subDescriptors[i].wDescriptorLength;
		}
	}

	state = FetchReportDescriptor;
	ctlurb._this = this;
	ctlurb.u.endpoint = device->getEndpoint(0);
	ctlurb.u.setup.bmRequestType = 0x81;
	ctlurb.u.setup.bRequest = 6;//GET DESCRIPTOR
	ctlurb.u.setup.wValue = (0x22 << 8);
	ctlurb.u.setup.wIndex = interfaceNumber;
	ctlurb.u.setup.wLength = report_desc_size;
	ctldata.resize(report_desc_size);
	ctlurb.u.buffer = ctldata.data();
	ctlurb.u.buffer_len = ctldata.size();
	ctlurb.u.completion = _ctlurbCompletion;

	usb::submitURB(&ctlurb.u);

	//a "report" is the content of the interrupt message.
	//there may be multiple different reports. if that is the case,
	//the report descriptor contains "Report ID" tags and the reports
	//have the 8 bit id as first byte. Otherwise, if there are no
	//"Report ID" tags, there is no report id byte in the report.


	//it looks like there is only ever one report descriptor, but we
	//need the size information.


	/* example descriptor: (my gamepad, at least 4 axis, 10 buttons, force feedback)
00000000  05 01         Usage Page #1 (Generic Desktop Controls)
00000002  09 04         Usage #0x01/0x04 (Joystick)
00000004  a1 01         Collection Application
00000006  09 01           Usage #0x01/0x01 (Pointer)
00000008  a1 00           Collection Physical
0000000a  85 01             Report ID 0x01
0000000c  09 30             Usage #0x01/0x30 (X)
0000000e  15 00             Logical Minimum 0
00000010  26 ff 00          Logical Maximum 0x00ff
00000013  35 00             Physical Minimum 0
00000015  46 ff 00          Physical Maximum 0x00ff
00000018  75 08             Report Size 8
0000001a  95 01             Report Count 1
0000001c  81 02             Input Data, Variable, Absolute, No Wrap, Linear, Prefferred state, No Null position, Bit field
0000001e  09 31             Usage #0x01/0x31 (Y)
00000020  81 02             Input Data, Variable, Absolute, No Wrap, Linear, Prefferred state, No Null position, Bit field
00000022  05 02             Usage Page #2 (Simulation Controls)
00000024  09 ba             Usage #0x02/0xba (Rudder)
00000026  81 02             Input Data, Variable, Absolute, No Wrap, Linear, Prefferred state, No Null position, Bit field
00000028  09 bb             Usage #0x02/0xbb (Throttle)
0000002a  81 02             Input Data, Variable, Absolute, No Wrap, Linear, Prefferred state, No Null position, Bit field
0000002c  05 09             Usage Page #9 (Buttons)
0000002e  19 01             Usage Minimum #0x09/0x01 (Button 1)
00000030  29 0c             Usage Maximum #0x09/0x0c (Button 12)
00000032  25 01             Logical Maximum 1
00000034  45 01             Physical Maximum 1
00000036  75 01             Report Size 1
00000038  95 0c             Report Count 12
0000003a  81 02             Input Data, Variable, Absolute, No Wrap, Linear, Prefferred state, No Null position, Bit field
0000003c  95 01             Report Count 1
0000003e  75 00             Report Size 0 (0?)
00000040  81 03             Input Constant, Variable, Absolute, No Wrap, Linear, Prefferred state, No Null position, Bit field
00000042  05 01             Usage Page #1 (Generic Desktop Controls)
00000044  09 39             Usage #0x01/0x39 (Hat switch)
00000046  25 07             Logical Maximum 7
00000048  46 3b 01          Physical Maximum 0x13b
0000004b  55 00             Unit Exponent 0
0000004d  65 44             Unit 0x44 (Degrees)
0000004f  75 04             Report Size 4
00000051  81 42             Input Data, Variable, Absolute, No Wrap, Linear, Prefferred state, Null position, Bit field
00000053  65 00             Unit 0
00000055  c0              End collection

00000056  05 0f           Usage Page #15 (Physical Interface Device)
00000058  09 92           Usage #0x0f/0x92 (PID State Report)
0000005a  a1 02           Collection Logical
0000005c  85 02             Report ID 0x02
0000005e  09 a0             Usage #0x0f/0xa0 (Actuators Enabled)
00000060  09 9f             Usage #0x0f/0x9f (Device Paused)
00000062  25 01             Logical Maximum 1
00000064  45 00             Physical Maximum 0
00000066  75 01             Report Size 1
00000068  95 02             Report Count 2
0000006a  81 02             Input Data, Variable, Absolute, No Wrap, Linear, Prefferred state, No Null position, Bit field
0000006c  75 06             Report Size 6
0000006e  95 01             Report Count 1
00000070  81 03             Input Constant, Variable, Absolute, No Wrap, Linear, Prefferred state, No Null position, Bit field
00000072  09 22             Usage #0x0f/0x22 (Effect Block Index)
00000074  75 07             Report Size 7
00000076  25 7f             Logical Maximum 0x7f
00000078  81 02             Input Data, Variable, Absolute, No Wrap, Linear, Prefferred state, No Null position, Bit field
0000007a  09 94             Usage #0x0f/0x94 (Effect Playing)
0000007c  75 01             Report Size 1
0000007e  25 01             Logical Maximum 0x7f
00000080  81 02             Input Data, Variable, Absolute, No Wrap, Linear, Prefferred state, No Null position, Bit field
00000082  c0              End Collection

00000083  09 21           Usage #0x0f/0x21 (Set Effect Report)
00000085  a1 02           Collection Logical
00000087  85 0b             Report ID 0x0b
00000089  09 22             Usage #0x0f/0x22 (Effect Block Index)
0000008b  26 ff 00          Logical Maximum 0xff
0000008e  75 08             Report Size 8
00000090  91 02             Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000092  09 53             Usage #0x0f/0x53 (Trigger Button)
00000094  25 0a             Logical Maximum 0xa
00000096  91 02             Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000098  09 50             Usage #0x0f/0x50   (Duration)
0000009a  27 fe ff 00 00    Logical Maximum 0x0000fffe
0000009f  47 fe ff 00 00    Physical Maximum 0x0000fffe
000000a4  75 10             Report Size 16
000000a6  55 fd             Unit Exponent -3
000000a8  66 01 10          Unit 0x1001 (Seconds)
000000ab  91 02             Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
000000ad  55 00             Unit Exponent 0
000000af  65 00             Unit 0
000000b1  09 54             Usage #0x0f/0x54 (Trigger Repeat Interval)
000000b3  55 fd             Unit Exponent -3
000000b5  66 01 10          Unit 0x1001 (Seconds)
000000b8  91 02             Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
000000ba  55 00             Unit Exponent 0
000000bc  65 00             Unit 0
000000be  09 a7             Usage #0x0f/0xa7 (Start Delay)
000000c0  55 fd             Unit Exponent -3
000000c2  66 01 10          Unit 0x1001 (Seconds)
000000c5  91 02             Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
000000c7  55 00             Unit Exponent 0
000000c9  65 00             Unit 0
000000cb  c0              End Collection

000000cc  09 5a           Usage #0x0f/0x5a (Set Envelope Report)
000000ce  a1 02           Collection Logical
000000d0  85 0c             Report ID 0x0c
000000d2  09 22             Usage #0x0f/0x22 (Effect Block Index)
000000d4  26 ff 00          Logical Maximum 0xff
000000d7  45 00             Physical Maximum 0
000000d9  75 08             Report Size 8
000000db  91 02             Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
000000dd  09 5c             Usage #0x0f/0x5c (Attack Time)
000000df  26 10 27          Logical Maximum 0x2710
000000f2  46 10 27          Physical Maximum 0x2710
000000f5  75 10             Report Size 16
000000f7  55 fd             Unit Exponent -3
000000f9  66 01 10          Unit 0x1001 (Seconds)
000000fc  91 02             Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
000000fe  55 00             Unit Exponent 0
000000f0  65 00             Unit 0
000000f2  09 5b             Usage #0x0f/0x5b (Attack Level)
000000f4  25 7f             Logical Maximum 0x7f
000000f6  75 08             Report Size 8
000000f8  91 02             Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
000000fa  09 5e             Usage #0x0f/0x5e (Fade Time)
000000fc  26 10 27          Logical Maximum 0x2710
000000ff  75 10             Report Size 16
00000101  55 fd             Unit Exponent -3
00000103  66 01 10          Unit 0x1001 (Seconds)
00000106  91 02             Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000108  55 00             Unit Exponent 0
0000010a  65 00             Unit 0
0000010c  09 5d             Usage #0x0f/0x0f/0x5d (Fade Level)
0000010e  25 7f             Logical Maximum 0x7f
00000110  75 08             Report Size 8
00000112  91 02             Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000114  c0              End Collection

00000115  09 73           Usage #0x0f/0x73 (Set Constant Force)
00000117  a1 02           Collection Logical
00000119  85 0d             Report ID 0x0d
00000121  09 22             Usage #0x0f/0x22 (Effect Block Index)
00000123  26 ff 00          Logical Maximum 0xff
00000120  45 00             Physical Maximum 0
00000122  91 02             Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000124  09 70             Usage #0x0f/0x70 (Magnitude)
00000126  15 81             Logical Minimum -0x7f
00000128  25 7f             Logical Maximum 0x7f
0000012a  36 f0 d8          Physical Minimum -0x2710
0000012d  46 10 27          Physical Maximum 0x2710
00000130  91 02             Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000132  c0              End Collection

00000133  09 6e           Usage #0x0f/0x6e (Set Periodic Report)
00000135  a1 02           Collection Logical
00000137  85 0e             Report ID 0x0e
00000139  09 22             Usage #0x0f/0x22 (Effect Block Index)
0000013b  15 00             Logical Minimum 0
0000013d  26 ff 00          Logical Maximum 0xff
00000140  35 00             Physical Minimum 0
00000142  45 00             Physical Maximum 0
00000144  91 02             Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000146  09 70             Usage #0x0f/0x70 (Magnitude)
00000148  25 7f             Logical Maximum 0x7f
0000014a  46 10 27          Physical Maximum 0x2710
0000014d  91 02             Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
0000014f  09 6f             Usage #0x0f/0x6f (Offset)
00000151  15 81             Logical Minimum -0x7f
00000153  36 f0 d8          Physical Minimum -0x2710
00000156  91 02             Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000158  09 71             Usage #0x0f/0x71 (Phase)
0000015a  15 00             Logical Minimum 0
0000015c  26 ff 00          Logical Maximum 0xff
0000015f  35 00             Physical Minimum 0
00000161  46 68 01          Physical Maximum 0x168
00000164  91 02             Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000166  09 72             Usage #0x0f/0x72 (Period)
00000168  75 10             Report Size 16
0000016a  26 10 27          Logical Maximum 0x2710
0000016d  46 10 27          Physical Maximum 0x2710
00000170  55 fd             Unit Exponent -3
00000172  66 01 10          Unit 0x1001 (Seconds)
00000175  91 02             Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000177  55 00             Unit Exponent 0
00000179  65 00             Unit 0
0000017b  c0              End Collection

0000017c  09 77           Usage #0x0f/0x77 (Effect Operation Report)
0000017e  a1 02           Collection Logical
00000180  85 51             Report ID 0x51
00000182  09 22             Usage #0x0f/0x22   (Effect Block Index)
00000184  25 7f             Logical Maximum 0x7f
00000186  45 00             Physical Maximum 0
00000188  75 08             Report Size 8
0000018a  91 02             Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
0000018c  09 78             Usage #0x0f/0x78 (Effect Operation)
0000018e  a1 02             Collection Logical
00000190  09 7b               Usage #0x0f/0x7b (Op Effect Stop)
00000192  09 79               Usage #0x0f/0x79 (Op Effect Start)
00000194  09 7a               Usage #0x0f/0x7a (Op Effect Start Solo)
00000196  15 01               Logical Minimum 1
00000198  25 03               Logical Maximum 3
0000019a  91 00               Output Data, Array, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
0000019c  c0                End Collection
0000019d  09 7c             Usage #0x0f/0x7c (Loop Count)
0000019f  15 00             Logical Minimum 0
000001a1  26 fe 00          Logical Maximum 0x00fe
000001a4  91 02             Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
000001a6  c0              End Collection

000001a7  09 92           Usage #0x0f/0x92 (PID State Report)
000001a9  a1 02           Collection Logical
000001ab  85 52             Report ID 0x52
000001ad  09 96             Usage #0x0f/0x96 (PID Device Control)
000001af  a1 02             Collection Logical
000001b1  09 9a               Usage #0x0f/0x9a (DC Device Reset)
000001b3  09 99               Usage #0x0f/0x99 (DC Stop All Effects)
000001b5  09 97               Usage #0x0f/0x97 (DC Enable Actuators)
000001b7  09 98               Usage #0x0f/0x98 (DC Disable Actuators)
000001b9  09 9b               Usage #0x0f/0x9b (DC Device Pause)
000001bb  09 9c               Usage #0x0f/0x9c (DC Device Continue)
000001bd  15 01               Logical Minimum 1
000001bf  25 06               Logical Maximum 6
000001c1  91 00               Output Data, Array, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
000001c3  c0                End Collection
000001c4  c0              End Collection

000001c5  05 ff           Usage Page 0xffff (Vendor defined)
000001c7  0a 01 03        Usage #0xffff/0x301 (Vendor defined)
000001ca  a1 02           Collection Logical
000001cc  85 40             Report ID 0x40
000001ce  0a 02 03          Usage #0xffff/0x301 (Vendor defined)
000001d1  a1 02             Collection Logical
000001d3  1a 11 03            Usage Minimum #0xffff/0x3011 (Vendor defined)
000001d6  2a 20 03            Usage Maximum #0xffff/0x3020 (Vendor defined)
000001d9  25 10               Logical Maximum 16
000001db  91 00               Output Data, Array, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
000001dd  c0                End Collection
000001de  0a 03 03          Usage #0xffff/0x303 (Vendor defined)
000001e1  15 00             Logical Minimum 0
000001e3  27 ff ff 00 00    Logical Maximum 0x0000ffff
000001e8  75 10             Report Size 16
000001ea  91 02             Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
000001ec  c0              End Collection

000001ed  05 0f           Usage Page #15 (Physical Interface Device)
000001ef  09 7d           Usage #0x0f/0x7d (Device Gain Report)
000001f1  a1 02           Collection Logical
000001f3  85 43             Report ID 0x43
000001f5  09 7e             Usage #0x0f/0x7e (Device Gain)
000001f7  26 80 00          Logical Maximum 0x0080
000001fa  46 10 27          Physical Maximum 0x2710
000001fd  75 08             Report Size 8
000001ff  91 02             Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000201  c0              End Collection

00000202  09 7f           Usage #0x0f/0x7f (PID Pool Report)
00000204  a1 02           Collection Logical
00000206  85 0b             Report ID 0x0b
00000208  09 80             Usage #0x0f/0x80 (RAM Pool Size)
0000020a  26 ff 7f          Logical Maximum 0x7fff
0000020d  45 00             Physical Maximum 0
0000020f  75 0f             Report Size 15
00000211  b1 03             Feature Constant, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000213  09 a9             Usage #0x0f/0xa9 (Device Managed Pool)
00000215  25 01             Logical Maximum 1
00000217  75 01             Report Size 1
00000219  b1 03             Feature Constant, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
0000021b  09 83             Usage #0x0f/0x83 (Simultaneous Effect Max)
0000021d  26 ff 00          Logical Maximum 0x00ff
00000220  75 08             Report Size 8
00000222  b1 03             Feature Constant, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000224  c0              End Collection

00000225  09 ab           Usage #0x0f/0xab (Create New Effect Report)
00000227  a1 03           Collection Report
00000229  85 15             Report ID 0x15
0000022b  09 25             Usage #0x0f/0x25 (Effect Type)
0000022d  a1 02             Collection Logical
0000022f  09 26               Usage #0x0f/0x26 (ET Constant Force)
00000231  09 30               Usage #0x0f/0x30 (ET Square)
00000233  09 32               Usage #0x0f/0x32 (ET Triangle)
00000235  09 31               Usage #0x0f/0x31 (ET Sine)
00000237  09 33               Usage #0x0f/0x33 (ET Sawtooth Up)
00000239  09 34               Usage #0x0f/0x34 (ET Sawtooth Down)
0000023b  15 01               Logical Minimum 1
0000023d  25 06               Logical Maximum 6
0000023f  b1 00               Feature Data, Array, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000241  c0                End Collection
00000242  c0              End Collection

00000243  09 89           Usage #0x0f/0x89 (PID Block Load Report)
00000245  a1 03           Collection Report
00000247  85 16             Report ID 0x16
00000249  09 8b             Usage #0x0f/0x8b (Block Load Status)
0000024b  a1 02             Collection Logical
0000024d  09 8c               Usage #0x0f/0x8c (Block Load Success)
0000024f  09 8d               Usage #0x0f/0x8d (Block Load Full)
00000251  09 8e               Usage #0x0f/0x8e (Block Load Error)
00000253  25 03               Logical Maximum 3
00000255  b1 00               Feature Data, Array, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000257  c0                End Collection
00000258  09 22             Usage #0x0f/0x22   (Effect Block Index)
0000025a  15 00             Logical Minimum 0
0000025c  26 fe 00          Logical Maximum 0x00fe
0000025f  b1 02             Feature Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000261  c0              End Collection

00000262  09 90           Usage #0x0f/0x90   (PID Block Free Report)
00000264  a1 03           Collection Report
00000266  85 50             Report ID 0x50
00000268  09 22             Usage #0x0f/0x22   (Effect Block Index)
0000026a  26 ff 00          Logical Maximum 0x00ff
0000026d  91 02             Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
0000026f  c0              End Collection
00000270  c0            End Collection


         => 11 Inputs
         => 25 Outputs
         => 6 Features
         => 20 Collections
         => 2 Input Reports, 9 Output Reports, 3 Feature Reports
         => 73 Usages(20 Collections, 9 Input, 31 Output, 13 Feature)
	 */
}

void USBHIDDev::disconnected(RefPtr<usb::Device> /*device*/) {
	state = Disconnected;
	usb::retireURB(&ctlurb.u);
	ctlurb.u.endpoint = NULL;
	usb::retireURB(&irqurb.u);
	irqurb.u.endpoint = NULL;
	deviceRemove();
	delete this;
}

std::vector<input::Report> USBHIDDev::getCurrentInputReports() const {
	std::vector<input::Report> res;
	for(auto const & rep : inputreports) {
		for(auto const &elem : rep->elements) {
			input::Report hir;
			InOut *inout = elem.inout;
			//set some good defaults
			hir.device = const_cast<USBHIDDev*>(this);
			hir.flags = 0;
			if(inout->flags & USBHID_MAINFLAG_RELATIVE)
				hir.flags |= input::Report::Relative;
			if(inout->flags & USBHID_MAINFLAG_WRAP)
				hir.flags |= input::Report::Wraps;
			if(inout->flags & USBHID_MAINFLAG_NON_LINEAR)
				hir.flags |= input::Report::Nonlinear;
			if(inout->flags & USBHID_MAINFLAG_NO_PREFERRED)
				hir.flags |= input::Report::NoPreferredState;
			if(inout->flags & USBHID_MAINFLAG_NULL_STATE)
				hir.flags |= input::Report::HasNullState;
			hir.logical_maximum = inout->logical_maximum;
			hir.logical_minimum = inout->logical_minimum;
			hir.physical_maximum = inout->physical_maximum;
			hir.physical_minimum = inout->physical_minimum;
			hir.unit = inout->unit;
			hir.unit_exponent = inout->unit_exponent;
			if(inout->flags & USBHID_MAINFLAG_VARIABLE) {
				for(unsigned i = 0; i < inout->report_count; i++) {
					if (i < inout->last_values.size())
						hir.value = inout->last_values[i];
					else
						hir.value = 0;
					hir.usage = inout->usages[0].getUsage(i);
					res.push_back(hir);
				}
			} else {
				//report assertion for all valid in last_values.
				for(int v = inout->logical_minimum;
						v <= inout->logical_maximum;
						v++) {

					bool found = false;
					for(auto &lv : inout->last_values) {
						if(lv == v) {
							found = true;
							break;
						}
					}
					hir.usage = inout->usages[0].getUsage
						    (v-inout->logical_minimum);
					hir.value = found?1:0;

					res.push_back(hir);
				}
			}
		}
	}
	return res;
}

uint32_t USBHIDDev::countryCode() const {
	return hiddescriptor->bCountryCode;
}

std::string USBHIDDev::name() const {
	std::stringstream str;
	if(device->manufacturer().empty()) {
		unsigned int vendor = device->getDeviceDescriptor().idVendor;
		str << std::hex << std::setw(4) << std::setfill('0') << vendor;
	} else {
		str << device->manufacturer();
	}
	str << " ";
	if(device->product().empty()) {
		unsigned int product = device->getDeviceDescriptor().idProduct;
		str << std::hex << std::setw(4) << std::setfill('0') << product;
	} else {
		str << device->product();
	}
	return str.str();
}

static USBHID usbhid_driver;

void USBHID_Setup() {
	usb::registerDriver(&usbhid_driver);
}
/*
 * UsageInfo: 5 words+dynamic(uint32_t)
 * StringInfo: 4 words+dynamic(0*uint8_t)
 * DesignatorInfo: 4 words+dynamic(0*uint8_t)
 * InOut: 15 words+StringInfo+DesignatorInfo+dynamic(1*UsageInfo,int32_t)
 * Collection: 5 words (for inputs: 3 words+dynamic(pointer per InOut))+(for outputs: 3 words+dynamic(pointer per InOut))+(for features: 3 words+dynamic(pointer per InOut))
 * Report:  4 words+dynamic(2 words per referenced InOut)


         => 11 Inputs
         => 25 Outputs
         => 6 Features
         => 20 Collections
         => 2 Input Reports, 9 Output Reports, 3 Feature Reports
         => 73 Usages(20 Collections, 9 Input, 31 Output, 13 Feature)

 * InOut: (20+8 words*11 for inputs)+(20+8 words * 25 for outputs)+(20+8 words * 6 for features)
 * Collection: 100 words (for inputs: 60 words)+(for outputs: 60 words)+(for features: 60 words)
 * Report:  (8 words for inputs)+(36 words for outputs)+(12 words for features)

 dynamic usage:
 * UsageInfo: 20 words +(for inputs: 9 words)+(for outputs: 31 words)+(for features: 13 words)
 * Collection: (for inputs: 11 InOut)+(for outputs: 25 InOut)+(for features: 6 InOut)
 * Report:  (for inputs: 22 words)+(for outputs: 50 words)+(for features: 12 words)

 support all: 1669 words (6.5kB) <-- this should not deplete memory! => need to look closer what happens during parse.
 support for input only: 439 words (1.8kB)

 */
