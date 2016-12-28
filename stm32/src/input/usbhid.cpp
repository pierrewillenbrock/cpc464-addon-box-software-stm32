

#include <input/usbhid.h>
#include <usb/usb.hpp>
#include <usb/usbdevice.hpp>
#include <usb/usbendpoint.hpp>
#include <vector>
#include <deque>
#include <bits.h>
#include <input/input.hpp>

#include "usbhidproto.h"

namespace usbhid {

	struct UsageInfo {
		uint32_t usage_min;
		uint32_t usage_max;
		std::deque<uint32_t> usages;
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
	};

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
	};

	struct GlobalState {
		uint16_t usage_page;
		int32_t logical_minimum;
		int32_t logical_maximum;
		int32_t physical_minimum;
		int32_t physical_maximum;
		int32_t unit_exponent;
		uint32_t unit;
		uint8_t report_size;
		uint8_t report_id;
		uint8_t report_count;
		GlobalState() {
			usage_page = 0;
			logical_minimum = 0;
			logical_maximum = 0;
			physical_minimum = 0;
			physical_maximum = 0;
			unit_exponent = 0;
			unit = 0;
			report_size = 0;
			report_id = 0;
			report_count = 1;
		}
	};

	struct LocalState {
		unsigned delimiter;
		std::vector<UsageInfo> usages;
		DesignatorInfo designators;
		StringInfo strings;
		void reset() {
			delimiter = ~0U;
			usages.clear();
			designators.designator_min = 0;
			designators.designator_max = 0;
			designators.designators.clear();
			strings.string_min = 0;
			strings.string_max = 0;
			strings.strings.clear();
		}
	};

	class Report;

	struct InOut {
		uint32_t flags;
		std::vector<UsageInfo> usages;
		DesignatorInfo designators;
		StringInfo strings;
		int32_t logical_minimum;
		int32_t logical_maximum;
		int32_t physical_minimum;
		int32_t physical_maximum;
		int32_t unit_exponent;
		uint32_t unit;
		uint32_t report_count;
		uint16_t control_info_index;
		Report *report;
		std::vector<int32_t> last_values;
	};

	class Collection { // this is a hierarchical view of the device
	public:
		uint32_t type;
		uint32_t usage;
		std::vector<Collection> collections;
		std::vector<InOut*> inputs;
		std::vector<InOut*> outputs;
		std::vector<InOut*> features;
		//the collection owns its InOut elements
		void clear() {
			for(auto &i : inputs)
				delete i;
			for(auto &o : outputs)
				delete o;
			for(auto &f : features)
				delete f;
			collections.clear();
			inputs.clear();
			outputs.clear();
			features.clear();
		}
		~Collection() {
			for(auto &i : inputs)
				delete i;
			for(auto &o : outputs)
				delete o;
			for(auto &f : features)
				delete f;
		}
	};

	class Report { // this is a decoding/encoding view of the reports
	public:
		uint8_t report_id;//using 0 to denote "not used".
		struct Element {
			uint32_t report_size;
			uint32_t report_count;
			uint32_t report_position;//in bits
			InOut *inout;
		};
		std::vector<Element> elements;
	};
}

using namespace usbhid;

class USBHID : public USBDriver {
public:
	virtual bool probe(RefPtr<USBDevice> device);
};

class USBHIDDev : public USBDriverDevice, public InputDev {
private:
	RefPtr<USBDevice> device;
	enum { None, FetchReportDescriptor, Configured
	} state;

	struct USBHIDDeviceURB {
		USBHIDDev *_this;
		URB u;
	} irqurb, ctlurb;
	std::vector<uint8_t> ctldata;
	std::vector<uint8_t> irqdata;
	USBHIDDescriptorHID *hiddescriptor;
	uint8_t input_endpoint;
	uint8_t input_polling_interval;
	void ctlurbCompletion(int result, URB *u);
	static void _ctlurbCompletion(int result, URB *u);
	void irqurbCompletion(int result, URB *u);
	static void _irqurbCompletion(int result, URB *u);
	void parseReportDescriptor(uint8_t *data, size_t size);

	std::vector<Report*> inputreports;
	std::vector<Report*> outputreports;
	std::vector<Report*> featurereports;
	std::vector<InOut*> inouts;
	Collection deviceCollection;

	void setValues(InOut *inout, std::vector<int32_t> &values);
public:
	USBHIDDev(RefPtr<USBDevice> device)
		: device(device)
		{}
	~USBHIDDev() {
		Input_deviceRemove(this);
	}
	virtual void interfaceClaimed(uint8_t interfaceNumber, uint8_t alternateSetting);
	virtual void disconnected(RefPtr<USBDevice> device);
	virtual InputControlInfo getControlInfo(uint16_t control_info_index);
};

static const uint16_t USBClassHID = 3;

bool USBHID::probe(RefPtr<USBDevice> device) {
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
	inputreports.clear();
	outputreports.clear();
	featurereports.clear();
	inouts.clear();


	std::vector<Collection*> collectionstack;
	std::vector<GlobalState> globals;
	globals.resize(1);
	LocalState locals;

	collectionstack.push_back(&deviceCollection);
	Report *inputreport = NULL;
	Report *outputreport = NULL;
	Report *featurereport = NULL;

	uint8_t *de = data + size;
	while(data < de) {
		uint8_t opcode = data[0];
		unsigned dataSize;
		unsigned tagtype;
		if (opcode == 0xfe) {
			//long item
			dataSize = data[1];
			//the type/tag is not defined for long items in the
			//spec, and not used.
			tagtype = data[2];
			data += 3;
		} else {
			//short item
			switch(opcode & 0x03) {
			case 0:	dataSize = 0; break;
			case 1:	dataSize = 1; break;
			case 2:	dataSize = 2; break;
			case 3:	dataSize = 4; break;
			}
			tagtype = opcode & 0xfc;
			data ++;
		}
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

		switch(tagtype) {
			//Main Items
		case 0x80: // Input
		case 0x90: // Output
		case 0xb0: { // Feature
			//this works for flags == 0.
			//array items may need a different representation
			std::vector<InOut*> &iovec =
				(tagtype == 0x80)?collectionstack.back()->inputs:
				(tagtype == 0x90)?collectionstack.back()->outputs:
				collectionstack.back()->features;
			Report *& repptr =
				(tagtype == 0x80)?inputreport:
				(tagtype == 0x90)?outputreport:
				featurereport;
			std::vector<Report*> & repvec =
				(tagtype == 0x80)?inputreports:
				(tagtype == 0x90)?outputreports:
				featurereports;
			InOut *item = new InOut();
			item->control_info_index = inouts.size();
			inouts.push_back(item);
			iovec.push_back(item);
			item->flags = argument;
			item->usages = locals.usages;
			item->designators = locals.designators;
			item->strings = locals.strings;
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
				repptr = new Report();
				repptr->report_id = globals.back().report_id;
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
		case 0xa0: { // Collection
			collectionstack.back()->collections.resize
				(collectionstack.back()->collections.size()+1);
			collectionstack.push_back
				(&collectionstack.back()->collections.back());
			collectionstack.back()->type = argument;
			collectionstack.back()->usage = locals.usages.back().getUsage(0);
			locals.reset();
			break;
		}
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
   base unit, in 2s complement.
   except nibble 0, which is the measuring system.
   nibble \ system   0      1            2            3            4
                 | None | SI Linear  | SI         | English    | English
                 |      |            | Rotation   | Linear     | Rotation
   1 Length      | -    | Centimeter | Radians    | Inch       | Degrees
   2 Mass        | -    | Gram       | Gram       | Slug       | Slug
   3 Time        | -    | Seconds    | Seconds    | Seconds    | Seconds
   4 Temperature | -    | Kelvin     | Kelvin     | Fahrenheit | Fahrenheit
   5 Current     | -    | Ampere     | Ampere     | Ampere     | Ampere
   6 Luminous    | -    | Candela    | Candela    | Candela    | Candela
     Intensity   |      |            |            |            |
   7 Reserved    |      |            |            |            |

   0x1001, 0x1002, 0x1003, 0x1004 => Seconds
   0x11 => Centimeters
   0xe121 => g*cm*cm/s/s => 1e-7 kg*m*m/s/s => 1e-7 Joule
*/
			globals.back().unit = argument;
			break;
		case 0x74: //Report Size
			globals.back().report_size = argument;
			break;
		case 0x84: { //Report ID
			globals.back().report_id = argument;
			//see if we can find a matching report in each of the
			//report types. the main items create a new one if
			//there is none, yet.
			inputreport = NULL;
			for(auto &r : inputreports) {
				if (r->report_id == argument)
					inputreport = r;
			}
			outputreport = NULL;
			for(auto &r : outputreports) {
				if (r->report_id == argument)
					outputreport = r;
			}
			featurereport = NULL;
			for(auto &r : featurereports) {
				if (r->report_id == argument)
					featurereport = r;
			}
			break;
		}
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
		case 0x08: { //Usage  (with dataSize = 4, the page is encoded in the high 16 bits)
			if (locals.usages.empty()) {
				locals.usages.resize(1);
			}
			if (dataSize < 4)
				argument |= (globals.back().usage_page << 16);
			locals.usages.back().usages.push_back(argument);
			break;
		}
		case 0x18: { //Usage Minimum
			if (locals.usages.empty()) {
				locals.usages.resize(1);
			}
			if (dataSize < 4)
				argument |= (globals.back().usage_page << 16);
			locals.usages.back().usage_min = argument;
			break;
		}
		case 0x28: { //Usage Maximum
			if (locals.usages.empty()) {
				locals.usages.resize(1);
			}
			if (dataSize < 4)
				argument |= (globals.back().usage_page << 16);
			locals.usages.back().usage_max = argument;
			break;
		}
		case 0x38: //Designator Index
			locals.designators.designators.push_back(argument);
			break;
		case 0x48: //Designator Minimum
			locals.designators.designator_min = argument;
			break;
		case 0x58: //Designator Maximum
			locals.designators.designator_max = argument;
			break;
		case 0x78: //String Index
			locals.strings.strings.push_back(argument);
			break;
		case 0x88: //String Minimum
			locals.strings.string_min = argument;
			break;
		case 0x98: //String Maximum
			locals.strings.string_max = argument;
			break;
		case 0xa8: { //Delimiter (used to define sets of alternative usages)
			if (argument == 1)
				locals.usages.resize(locals.usages.size()+1);
		}
		}

		//need to create three groups of reports: input, output, feature
		//all with or without report id. can use report id 0 to indicate
		//no use of the report id system.

		data += dataSize;
	}
}

void USBHIDDev::ctlurbCompletion(int result, URB *u) {
	if (result != 0) {
		//try again
		USB_submitURB(u);
		return;
	}
	switch(state) {
	case FetchReportDescriptor: {
		//yay, got the report!
		//now, parse it.
		parseReportDescriptor(ctldata.data(),u->buffer_received);
		//okay, got possibly a number of reports. find the max size of
		//input.
		size_t input_max_size = 0;
		for(auto & ir : inputreports) {
			size_t sz =
				ir->elements.back().report_position +
				ir->elements.back().report_size *
				ir->elements.back().report_count;
			if (input_max_size < sz)
				input_max_size = sz;
		}
		input_max_size = (input_max_size + 7)/8;
		//setup the URB
		irqurb._this = this;
		irqurb.u.endpoint = device->getEndpoint(input_endpoint);
		irqdata.resize(input_max_size);
		irqurb.u.pollingInterval = input_polling_interval;
		irqurb.u.buffer = irqdata.data();
		irqurb.u.buffer_len = irqdata.size();
		irqurb.u.completion = _irqurbCompletion;

		USB_submitURB(&irqurb.u);

		state = Configured;

		Input_deviceAdd(this);

		break;
	}
	default: assert(0); break;
	}
}

void USBHIDDev::_ctlurbCompletion(int result, URB *u) {
	USBHIDDeviceURB *du = container_of(u, USBHIDDeviceURB, u);
	du->_this->ctlurbCompletion(result, u);
}

void USBHIDDev::irqurbCompletion(int result, URB *u) {
	//this is called repeatedly by the usb subsystem.
	if (result != 0)
		return;
	//cool, lets look at the report.
	if (inputreports.empty())
		return;
	Report *rep = inputreports[0];
	uint8_t *data = irqdata.data();
	if (inputreports.size() > 1 ||
	    inputreports[0]->report_id != 0) {
		rep = NULL;
		for(auto &r : inputreports) {
			if (r->report_id == data[0])
				rep = r;
		}
		if (!rep)
			return;
		data++;
	}
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
			if (v & (m>>1)) //make it signed
				v -= m;
			values.push_back(v);
		}
		setValues(e.inout,values);
	}
}

void USBHIDDev::_irqurbCompletion(int result, URB *u) {
	USBHIDDeviceURB *du = container_of(u, USBHIDDeviceURB, u);
	du->_this->irqurbCompletion(result, u);
}

void USBHIDDev::setValues(InOut *inout, std::vector<int32_t> &values) {
	InputReport hir;
	//set some good defaults
	hir.device = this;
	hir.flags = 0;
	if (inout->flags & 0x4)
		hir.flags |= 0x1;
	if (inout->flags & 0x8)
		hir.flags |= 0x2;
	if (inout->flags & 0x10)
		hir.flags |= 0x4;
	if (inout->flags & 0x20)
		hir.flags |= 0x8;
	if (inout->flags & 0x40)
		hir.flags |= 0x10;
	hir.control_info_index = inout->control_info_index;
	if (inout->flags & 0x02) {
		for(unsigned i = 0; i < values.size(); i++) {
			if (i < inout->last_values.size() &&
			    inout->last_values[i] == values[i])
				continue;//did not change
			hir.usage = inout->usages[0].getUsage(i);
			hir.value = values[i];
			Input_reportInput(hir);
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
				hir.control_info_index = inout->control_info_index;
				Input_reportInput(hir);
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
				Input_reportInput(hir);
			}
		}
	}
	inout->last_values = values;
}

void USBHIDDev::interfaceClaimed(uint8_t interfaceNumber, uint8_t alternateSetting) {
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

	USB_submitURB(&ctlurb.u);

	//a "report" is the content of the interrupt message.
	//there may be multiple different reports. if that is the case,
	//the report descriptor contains "Report ID" tags and the reports
	//have the 8 bit id as first byte. Otherwise, if there are no
	//"Report ID" tags, there is no report id byte in the report.


	//it looks like there is only ever one report descriptor, but we
	//need the size information.


	/* example descriptor: (my gamepad, at least 4 axis, 10 buttons, force feedback)
00000000  05 01         Usage Page #1 (Generic Desktop Controls)
00000002  09 04         Usage #0x04 (Joystick)
00000004  a1 01         Collection Application
00000006  09 01         Usage #0x01 (Pointer)
00000008  a1 00         Collection Physical
0000000a  85 01         Report ID 1
0000000c  09 30         Usage #0x30 (X)
0000000e  15 00         Logical Minimum 0
00000010  26 ff 00      Logical Maximum 0x00ff
00000013  35 00         Physical Minimum 0
00000015  46 ff 00      Physical Maximum 0x00ff
00000018  75 08         Report Size 8
0000001a  95 01         Report Count 1
0000001c  81 02         Input Data, Variable, Absolute, No Wrap, Linear, Prefferred state, No Null position, Bit field
0000001e  09 31         Usage #0x31 (Y)
00000020  81 02         Input Data, Variable, Absolute, No Wrap, Linear, Prefferred state, No Null position, Bit field
00000022  05 02         Usage Page #2 (Simulation Controls)
00000024  09 ba         Usage #0xba (Rudder)
00000026  81 02         Input Data, Variable, Absolute, No Wrap, Linear, Prefferred state, No Null position, Bit field
00000028  09 bb         Usage #0xbb (Throttle)
0000002a  81 02         Input Data, Variable, Absolute, No Wrap, Linear, Prefferred state, No Null position, Bit field
0000002c  05 09         Usage Page #9 (Buttons)
0000002e  19 01         Usage Minimum #0x01 (Button 1)
00000030  29 0c         Usage Maximum #0x0c (Button 12)
00000032  25 01         Logical Maximum 1
00000034  45 01         Physical Maximum 1
00000036  75 01         Report Size 1
00000038  95 0c         Report Count 12
0000003a  81 02         Input Data, Variable, Absolute, No Wrap, Linear, Prefferred state, No Null position, Bit field
0000003c  95 01         Report Count 1
0000003e  75 00         Report Size 0 (0?)
00000040  81 03         Input Constant, Variable, Absolute, No Wrap, Linear, Prefferred state, No Null position, Bit field
00000042  05 01         Usage Page #1 (Generic Desktop Controls)
00000044  09 39         Usage #0x39   (Hat switch)
00000046  25 07         Logical Maximum 7
00000048  46 3b 01      Physical Maximum 0x13b
0000004b  55 00         Unit Exponent 0
0000004d  65 44         Unit 44
0000004f  75 04         Report Size 4
00000051  81 42         Input Data, Variable, Absolute, No Wrap, Linear, Prefferred state, Null position, Bit field
00000053  65 00         Unit 0
00000055  c0            End collection

00000056  05 0f         Usage Page #15 (Physical Interface Device)
00000058  09 92         Usage #0x92 (PID State Report)
0000005a  a1 02         Collection Logical
0000005c  85 02         Report ID 2
0000005e  09 a0         Usage #0xa0  (Actuators Enabled)
00000060  09 9f         Usage #0x9f  (Device Paused)
00000062  25 01         Logical Maximum 1
00000064  45 00         Physical Maximum 0
00000066  75 01         Report Size 1
00000068  95 02         Report Count 2
0000006a  81 02         Input Data, Variable, Absolute, No Wrap, Linear, Prefferred state, No Null position, Bit field
0000006c  75 06         Report Size 6
0000006e  95 01         Report Count 1
00000070  81 03         Input Constant, Variable, Absolute, No Wrap, Linear, Prefferred state, No Null position, Bit field
00000072  09 22         Usage #0x22   (Effect Block Index)
00000074  75 07         Report Size 7
00000076  25 7f         Logical Maximum 0x7f
00000078  81 02         Input Data, Variable, Absolute, No Wrap, Linear, Prefferred state, No Null position, Bit field
0000007a  09 94         Usage #0x94   (Effect Playing)
0000007c  75 01         Report Size 1
0000007e  25 01         Logical Maximum 0x7f
00000080  81 02         Input Data, Variable, Absolute, No Wrap, Linear, Prefferred state, No Null position, Bit field
00000082  c0            End Collection
00000083  09 21         Usage #0x21   (Set Effect Report)
00000085  a1 02         Collection Logical
00000087  85 0b         Report ID 0x0b
00000089  09 22         Usage #0x22   (Effect Block Index)
0000008b  26 ff 00      Logical Maximum 0xff
0000008e  75 08         Report Size 8
00000090  91 02         Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000092  09 53         Usage #0x53   (Trigger Button)
00000094  25 0a         Logical Maximum 0xa
00000096  91 02         Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000098  09 50         Usage #0x50   (Duration)
0000009a  27 fe ff 00 00 Logical Maximum 0x0000fffe
0000009f  47 fe ff 00 00 Physical Maximum 0x0000fffe
000000a4  75 10         Report Size 16
000000a6  55 fd         Unit Exponent -3
000000a8  66 01 10      Unit 0x1001
000000ab  91 02         Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
000000ad  55 00         Unit Exponent 0
000000af  65 00         Unit 0
000000b1  09 54         Usage #0x54   (Trigger Repeat Interval)
000000b3  55 fd         Unit Exponent -3
000000b5  66 01 10      Unit 0x1001
000000b8  91 02         Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
000000ba  55 00         Unit Exponent 0
000000bc  65 00         Unit 0
000000be  09 a7         Usage #0xa7   (Start Delay)
000000c0  55 fd         Unit Exponent -3
000000c2  66 01 10      Unit 0x1001
000000c5  91 02         Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
000000c7  55 00         Unit Exponent 0
000000c9  65 00         Unit 0
000000cb  c0            End Collection
000000cc  09 5a         Usage #0x5a  (Set Envelope Report)
000000ce  a1 02         Collection Logical
000000d0  85 0c         Report ID 0x0c
000000d2  09 22         Usage #0x22  (Effect Block Index)
000000d4  26 ff 00      Logical Maximum 0xff
000000d7  45 00         Physical Maximum 0
000000d9  75 08         Report Size 8
000000db  91 02         Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
000000dd  09 5c         Usage #0x5c (Attack Time)
000000df  26 10 27      Logical Maximum 0x2710
000000f2  46 10 27      Physical Maximum 0x2710
000000f5  75 10         Report Size 16
000000f7  55 fd         Unit Exponent -3
000000f9  66 01 10      Unit 0x1001
000000fc  91 02         Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
000000fe  55 00         Unit Exponent 0
000000f0  65 00         Unit 0
000000f2  09 5b         Usage #0x5b (Attack Level)
000000f4  25 7f         Logical Maximum 0x7f
000000f6  75 08         Report Size 8
000000f8  91 02         Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
000000fa  09 5e         Usage #0x5e (Fade Time)
000000fc  26 10 27      Logical Maximum 0x2710
000000ff  75 10         Report Size 16
00000101  55 fd         Unit Exponent -3
00000103  66 01 10      Unit 0x1001
00000106  91 02         Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000108  55 00         Unit Exponent 0
0000010a  65 00         Unit 0
0000010c  09 5d         Usage #0x5d (Fade Level)
0000010e  25 7f         Logical Maximum 0x7f
00000110  75 08         Report Size 8
00000112  91 02         Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000114  c0            End Collection
00000115  09 73         Usage #0x73 (Set Constant Force)
00000117  a1 02         Collection Logical
00000119  85 0d         Report ID 0x0d
00000121  09 22         Usage #0x22 (Effect Block Index)
00000123  26 ff 00      Logical Maximum 0xff
00000120  45 00         Physical Maximum 0
00000122  91 02         Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000124  09 70         Usage #0x70 (Magnitude)
00000126  15 81         Logical Minimum -0x7f
00000128  25 7f         Logical Maximum 0x7f
0000012a  36 f0 d8      Physical Minimum 0x2710
0000012d  46 10 27      Physical Maximum 0x2710
00000130  91 02         Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000132  c0            End Collection
00000133  09 6e         Usage #0x6e (Set Periodic Report)
00000135  a1 02         Collection Logical
00000137  85 0e         Report ID 0x0e
00000139  09 22         Usage #0x22 (Effect Block Index)
0000013b  15 00         Logical Minimum 0
0000013d  26 ff 00      Logical Maximum 0xff
00000140  35 00         Physical Minimum 0
00000142  45 00         Physical Maximum 0
00000144  91 02         Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000146  09 70         Usage #0x70 (Magnitude)
00000148  25 7f         Logical Maximum 0x7f
0000014a  46 10 27      Physical Maximum 0x2710
0000014d  91 02         Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
0000014f  09 6f         Usage #0x6f (Offset)
00000151  15 81         Logical Minimum -0x7f
00000153  36 f0 d8      Physical Minimum -0x2710
00000156  91 02         Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000158  09 71         Usage #0x71 (Phase)
0000015a  15 00         Logical Minimum 0
0000015c  26 ff 00      Logical Maximum 0xff
0000015f  35 00         Physical Minimum 0
00000161  46 68 01      Physical Maximum 0x168
00000164  91 02         Output Data, Variable, Absolute, No Wrap, Linear, Preffered State, No Null positon, Non Volatile, Bit Field
00000166  09 72         Usage #0x72 (Period)
00000168  75 10         Report Size 0x10
0000016a  26 10 27      Logical Maximum 0x2710
0000016d  46 10 27      Physical Maximum 0x2710
00000170  55 fd
 66 01 10
 91 02
 55  00
 65 00
 c0
 09 77
 a1 02  |U.f....U.e...w..|
00000180  85 51
 09 22
 25 7f
 45 00
  75 08
 91 02
 09 78
 a1 02  |.Q."%.E.u....x..|
00000190  09 7b
 09 79
 09 7a
 15 01
  25 03
 91 00
c0
 09 7c
 15  000001a0  00
 26 fe 00
 91 02
 c0
 09  92
 a1 02
 85 52
 09 96
 a1 000001b0  02
 09 9a
 09 99
 09 97
 09 98
 09 9b
 09 9c
 15 01
 25 000001c0  06
 91 00
 c0
 c0
 05 ff
 0a 01 03
 a1 02
 85 40
 0a 02 000001d0  03
 a1 02
 1a 11 03
 2a 20 03
 25 10
 91 00
 c0
 0a 03  000001e0  03
 15 00
 27 ff ff 00 00
 75 10
 91 02
 c0
 05 0f
 09  000001f0  7d
 a1 02
 85 43
 09 7e
 26 80 00
 46 10 27
 75 08
 91  00000200  02
 c0
 09 7f
 a1 02
 85 0b
  09 80
 26 ff 7f
 45 00
 75  0000210  0f
 b1 03
 09 a9
 25 01
 75  01
 b1 03
 09 83
 26 ff 00
00000220  75 08
 b1 03
 c0
 09 ab
 a1  03
 85 15
 09 25
 a1 02
 09  00000230  26
 09 30
 09 32
 09 31
 09  33
 09 34
 15 01
 25 06
 b1  00000240  00
 c0
 c0
 09 89
 a1 03
 85  16
 09 8b
 a1 02
 09 8c
 09  00000250  8d
 09 8e
 25 03
 b1 00
 c0
 09 22
 15 00
 26 fe 00
 b1   00000260  02
 c0
 09 90
 a1 03
 85 50
 09 22
 26 ff 00
 91 02
 c0
00000270  c0                                                |.|

	 */
}

void USBHIDDev::disconnected(RefPtr<USBDevice> device) {
	ctlurb.u.endpoint = NULL;
	USB_retireURB(&irqurb.u);
	irqurb.u.endpoint = NULL;
}

InputControlInfo USBHIDDev::getControlInfo(uint16_t control_info_index) {
	InputControlInfo r;
	if (control_info_index >= inouts.size())
		return r;
	InOut *inout = inouts[control_info_index];
	r.logical_minimum = inout->logical_minimum;
	r.logical_maximum = inout->logical_maximum;
	r.physical_minimum = inout->physical_minimum;
	r.physical_maximum = inout->physical_maximum;
	r.unit_exponent = inout->unit_exponent;
	r.unit = inout->unit;
	r.flags = 0;
	if (inout->flags & 0x4)
		r.flags |= 0x1;
	if (inout->flags & 0x8)
		r.flags |= 0x2;
	if (inout->flags & 0x10)
		r.flags |= 0x4;
	if (inout->flags & 0x20)
		r.flags |= 0x8;
	if (inout->flags & 0x40)
		r.flags |= 0x10;
	return r;
}

static USBHID usbhid_driver;

void USBHID_Setup() {
	USB_registerDriver(&usbhid_driver);
}
