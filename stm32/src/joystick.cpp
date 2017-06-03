
#include <joystick.hpp>

#include <input/input.hpp>
#include <fpga/layout.h>
#include <ui/ui.hpp>
#include <bits.h>
#include <vector>
#include <algorithm>

static int const joystick_scale = 2;

class JoystickInput
	: public input::Listener
	, public Joystick {
private:
	JoystickEvent state;
	input::Device *dev;
public:
	JoystickInput(input::Device *dev);
	virtual void inputReport(input::Report const &rep);
	virtual std::string getName();
	virtual void remove(input::Device */*dev*/);
	JoystickInput();
};

class JoystickDevInput : public input::DeviceListener {
public:
	virtual void inputDeviceAdd(input::Device *dev);
};

JoystickInput::JoystickInput(input::Device *dev) :dev(dev) {
}

void JoystickInput::inputReport( input::Report const &rep) {
	if (rep.usage == 0x10030) {
		// X axis position
		//(v*254-127*max-127*min)/(max-min)=?
		state.axis[0] =
			(rep.value*254
			 - 127*(rep.logical_maximum+rep.logical_minimum))
			 /(rep.logical_maximum-rep.logical_minimum);
	}
	if (rep.usage == 0x10031) {
		// Y axis position
		state.axis[1] =
			(rep.value*254
			 - 127*(rep.logical_maximum+rep.logical_minimum))
			 /(rep.logical_maximum-rep.logical_minimum);
	}
	if ((rep.usage & 0xffff0000) == 0x90000) {
		// Buttons
		unsigned btn = rep.usage & 0xffff;
		if (btn >= 1 && btn <= 8) {
			if(rep.value)
				state.buttons |= 1 << (btn -1);
			else
				state.buttons &= ~(1 << (btn -1));
		}
	}
	m_onJoystickChange(state);
}

std::string JoystickInput::getName() {
	return dev->name();
}

static std::vector<RefPtr<JoystickInput> > joystickinputs;
static JoystickDevInput joystickdevinput;

void JoystickInput::remove(input::Device */*dev*/) {
	ISR_Guard g;
	auto it = std::find(joystickinputs.begin(), joystickinputs.end(), this);
	if (it == joystickinputs.end())
		return;
	joystickinputs.erase(it);
}

void JoystickDevInput::inputDeviceAdd(input::Device *dev) {
	/** \todo add joystick support, also ui control
	 */
	bool have_abs_x = false;
	bool have_abs_y = false;
	bool have_btn_1 = false;
	bool have_btn_2 = false;
	auto info = dev->getCurrentInputReports();
	for(auto const &rep : info) {
		if(!(rep.flags & input::Report::Relative) && rep.usage == 0x10030)
			have_abs_x = true;
		if(!(rep.flags & input::Report::Relative) && rep.usage == 0x10031)
			have_abs_y = true;
		if(rep.usage == 0x90001)
			have_btn_1 = true;
		if(rep.usage == 0x90002)
			have_btn_2 = true;
	}
	if (have_abs_x && have_abs_y && have_btn_1 && have_btn_2) {
		JoystickInput *j = new JoystickInput(dev);
		for(auto const &rep : info)
			j->inputReport(rep);
		//no need to track reference, we get a remove and remove
		//ourselves from joystickinputs in that case.
		dev->addListener(j);
		ISR_Guard g;
		joystickinputs.push_back(j);
	}
}

void Joystick_Setup() {
	input::registerDeviceListener(&joystickdevinput);
}

std::vector<RefPtr<Joystick> > Joystick_get() {
	std::vector<RefPtr<Joystick> > res;
	for(auto &j : joystickinputs) {
		res.push_back(j);
	}
	return res;
}

// kate: indent-width 8; indent-mode cstyle; replace-tabs off;
