
#include <joystick.hpp>

#include <input/input.hpp>
#include <fpga/layout.h>
#include <ui/ui.hpp>
#include <bits.h>
#include <vector>
#include <algorithm>

static void Joystick_DoUIEvents();

class JoystickInput
	: public input::Listener
	, public Joystick {
private:
	bool exclusive;
	JoystickEvent state;
	input::Device *dev;
	friend void Joystick_DoUIEvents();
public:
	JoystickInput(input::Device *dev);
	virtual void inputReport(input::Report const &rep);
	virtual std::string getName();
	virtual void remove(input::Device */*dev*/);
	virtual void setExclusive(bool exclusive);
	JoystickInput();
};

class JoystickDevInput : public input::DeviceListener {
public:
	virtual void inputDeviceAdd(input::Device *dev);
};

JoystickInput::JoystickInput(input::Device *dev) :dev(dev) {
	m_settings.axis_threshold = 63;
	m_settings.fire1_button = 0;
	m_settings.fire2_button = 1;
	m_settings.spare_button = 2;
	m_settings.mode_toggle_button = 3;
	m_settings.previous_button = 4;
	m_settings.next_button = 5;
	exclusive = false;
}

void JoystickInput::inputReport( input::Report const &rep) {
	if (rep.usage == 0x10030) {
		// X axis position
		//(v*254-127*max-127*min)/(max-min)=?
		int v = (rep.value*2-rep.logical_maximum-rep.logical_minimum)*127
			 /(rep.logical_maximum-rep.logical_minimum);
		if (v > 127)
			v = 127;
		if (v < -127)
			v = -127;
		state.axis[0] = v;
	}
	if (rep.usage == 0x10031) {
		// Y axis position
		int v = (rep.value*2-rep.logical_maximum-rep.logical_minimum)*127
			 /(rep.logical_maximum-rep.logical_minimum);
		if (v > 127)
			v = 127;
		if (v < -127)
			v = -127;
		state.axis[1] = v;
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
	if (!exclusive)
		Joystick_DoUIEvents();
	m_onJoystickChange(state);
}

std::string JoystickInput::getName() {
	return dev->name();
}

void JoystickInput::setExclusive(bool exclusive) {
	if(this->exclusive == exclusive)
		return;
	this->exclusive = exclusive;
}

static std::vector<RefPtr<JoystickInput> > joystickinputs;
static JoystickDevInput joystickdevinput;
static struct JoystickUIState {
	bool btn1:1;
	bool btn2:1;
	bool next:1;
	bool prev:1;
	int8_t axis[2];
} joystickUIState = { false, false, false, false, { 0, 0 } };

static void Joystick_DoUIEvents() {
	JoystickUIState newState = { false, false, false, false, {0, 0} };
	int axis[2] = {0,0};
	int axisthrs = 0;
	unsigned cnt = 0;
	for(auto const &jin : joystickinputs) {
		if (jin->exclusive)
			continue;
		axis[0] += jin->state.axis[0];
		axis[1] += jin->state.axis[1];
		axisthrs += jin->settings().axis_threshold;
		cnt++;
		if (jin->state.buttons & (1 << jin->settings().fire1_button))
			newState.btn1 = true;
		if (jin->state.buttons & (1 << jin->settings().fire2_button))
			newState.btn2 = true;
		if (jin->state.buttons & (1 << jin->settings().previous_button))
			newState.prev = true;
		if (jin->state.buttons & (1 << jin->settings().next_button))
			newState.next = true;
	}
	if (cnt) {
		int v;
		v = axis[0] / cnt;
		if (v > 127)
			v = 127;
		if (v < -127)
			v = -127;
		newState.axis[0] = v;
		v = axis[1] / cnt;
		if (v > 127)
			v = 127;
		if (v < -127)
			v = -127;
		newState.axis[1] = v;
		axisthrs /= cnt;
	} else
		axisthrs = 63;
	if(newState.btn1 && !joystickUIState.btn1)
		UI_joyTrgDown(ui::JoyTrg::Btn1);
	if(!newState.btn1 && joystickUIState.btn1)
		UI_joyTrgUp(ui::JoyTrg::Btn1);
	if(newState.btn2 && !joystickUIState.btn2)
		UI_joyTrgDown(ui::JoyTrg::Btn2);
	if(!newState.btn2 && joystickUIState.btn2)
		UI_joyTrgUp(ui::JoyTrg::Btn2);
	if(newState.prev && !joystickUIState.prev)
		UI_joyTrgDown(ui::JoyTrg::Previous);
	if(!newState.prev && joystickUIState.prev)
		UI_joyTrgUp(ui::JoyTrg::Previous);
	if(newState.next && !joystickUIState.next)
		UI_joyTrgDown(ui::JoyTrg::Next);
	if(!newState.next && joystickUIState.next)
		UI_joyTrgUp(ui::JoyTrg::Next);
	if (newState.axis[0] >= axisthrs &&
		joystickUIState.axis[0] < axisthrs)
		UI_joyTrgDown(ui::JoyTrg::Right);
	if (newState.axis[0] < axisthrs &&
		joystickUIState.axis[0] >= axisthrs)
		UI_joyTrgUp(ui::JoyTrg::Right);
	if (newState.axis[0] <= -axisthrs &&
		joystickUIState.axis[0] > -axisthrs)
		UI_joyTrgDown(ui::JoyTrg::Left);
	if (newState.axis[0] > -axisthrs &&
		joystickUIState.axis[0] <= -axisthrs)
		UI_joyTrgUp(ui::JoyTrg::Left);
	if (newState.axis[1] >= axisthrs &&
		joystickUIState.axis[1] < axisthrs)
		UI_joyTrgDown(ui::JoyTrg::Down);
	if (newState.axis[1] < axisthrs &&
		joystickUIState.axis[1] >= axisthrs)
		UI_joyTrgUp(ui::JoyTrg::Down);
	if (newState.axis[1] <= -axisthrs &&
		joystickUIState.axis[1] > -axisthrs)
		UI_joyTrgDown(ui::JoyTrg::Up);
	if (newState.axis[1] > -axisthrs &&
		joystickUIState.axis[1] <= -axisthrs)
		UI_joyTrgUp(ui::JoyTrg::Up);
	if(newState.axis[0] != joystickUIState.axis[0] ||
	                newState.axis[1] != joystickUIState.axis[1])
		UI_joyAxis(newState.axis[0], newState.axis[1]);
	joystickUIState = newState;
}

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
