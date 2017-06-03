
#pragma once

#include <vector>
#include <sigc++/sigc++.h>
#include <refcounted.hpp>

struct JoystickEvent {
	uint8_t buttons;//we support 8, first is at bit 0
	int8_t axis[2];//we only need to support 2 axis
};

class Joystick : public virtual Refcounted<Joystick> {
protected:
	sigc::signal<void,JoystickEvent> m_onJoystickChange;
public:
	virtual ~Joystick() {}
	virtual std::string getName() = 0;
	sigc::signal<void,JoystickEvent> &onJoystickChange() {
		return m_onJoystickChange;
	}
};

void Joystick_Setup();
std::vector<RefPtr<Joystick> > Joystick_get();
