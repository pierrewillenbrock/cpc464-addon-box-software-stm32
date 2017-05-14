

#include <input/usbhid.h>
#include <usb/usb.hpp>
#include <usb/usbdevice.hpp>
#include <usb/usbendpoint.hpp>
#include <vector>
#include <deque>
#include <bits.h>
#include <ui/notify.hpp>
#include <usbdevicenotify.h>
#include <sstream>
#include <iomanip>

class USBDeviceNotify : public USBDriver {
public:
	virtual bool probe(RefPtr<USBDevice> device);
};

bool USBDeviceNotify::probe(RefPtr<USBDevice> device) {
	std::stringstream ss;
	ss << "USB Device " << device->manufacturer() << " " << device->product()
	<< "(" << std::setw(4) << std::setfill('0') << std::hex << device->getDeviceDescriptor().idVendor
	<< ":" << std::setw(4) << std::setfill('0') << std::hex << device->getDeviceDescriptor().idProduct
	<< ") inserted";
	ui::Notification_Add(ss.str());
	return false;
}

static USBDeviceNotify usbdevicenotify_driver;

void USBDeviceNotify_Setup() {
	USB_registerDriver(&usbdevicenotify_driver);
}
