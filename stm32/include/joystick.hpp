
#pragma once

#include <vector>
#include <sigc++/sigc++.h>
#include <refcounted.hpp>

struct JoystickSettings {
	uint8_t mode_toggle_button:4; //for switching back to the UI
	uint8_t next_button:4;        //for going to the next UI item(like TAB on keyboard)
	uint8_t previous_button:4;    //for going to the next UI item(like shift+TAB on keyboard)
	uint8_t fire1_button:4;       //CPC fire1 or UI btn1
	uint8_t fire2_button:4;       //CPC fire2 or UI btn2
	uint8_t spare_button:4;       //CPC spare line, when used as a third fire
	int8_t axis_threshold;
};

struct JoystickEvent {
	uint8_t buttons;//we support 8, first is at bit 0
	int8_t axis[2];//we only need to support 2 axis
};

class Joystick : public virtual Refcounted<Joystick> {
protected:
	sigc::signal<void,JoystickEvent> m_onJoystickChange;
	JoystickSettings m_settings;
public:
	virtual ~Joystick() {}
	virtual std::string getName() = 0;
	sigc::signal<void,JoystickEvent> &onJoystickChange() {
		return m_onJoystickChange;
	}
	virtual void setExclusive(bool exclusive) = 0;
	void setSettings(JoystickSettings const &settings) {
		m_settings = settings;
	}
	JoystickSettings const & settings() const {
		return m_settings;
	}
};

void Joystick_Setup();
std::vector<RefPtr<Joystick> > Joystick_get();
