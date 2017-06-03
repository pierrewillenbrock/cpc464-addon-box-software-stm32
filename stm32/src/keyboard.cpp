
#include <keyboard.hpp>

#include <input/input.hpp>
#include <fpga/layout.h>
#include <ui/ui.hpp>
#include <bits.h>

static int const keyboard_scale = 2;

class KeyboardInput : public input::Listener {
public:
	virtual void inputReport(input::Report const &rep);
	KeyboardInput();
};

class KeyboardDevInput : public input::DeviceListener {
public:
	virtual void inputDeviceAdd(input::Device *dev);
};

KeyboardInput::KeyboardInput() {
}

void KeyboardInput::inputReport( input::Report const &rep) {
	if ((rep.usage & 0xffff0000) == 0x70000) {
		// key code
	}
}

static KeyboardInput keyboardinput;
static KeyboardDevInput keyboarddevinput;

void KeyboardDevInput::inputDeviceAdd(input::Device *dev) {
	/** \todo add keyboard support, also ui control
	 * \todo add keyboard ui control
	 */
	unsigned keycount = 0;//anything from the Key Code usage page
	auto info = dev->getCurrentInputReports();
	for(auto const &rep : info) {
		if((rep.usage & 0xffff0000) == 0x70000)
			keycount++;
	}
	if (keycount > 5)//arbitrary.
		dev->addListener(&keyboardinput);
}

void Keyboard_Setup() {
	keyboardinput.refIsStatic();
	input::registerDeviceListener(&keyboarddevinput);
}

// kate: indent-width 8; indent-mode cstyle; replace-tabs off;
