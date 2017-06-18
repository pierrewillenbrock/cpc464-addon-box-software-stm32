
#pragma once

#include <joystick.hpp>
#include <mouse.hpp>

namespace joyport {

struct MouseSettings {
	uint8_t mode_toggle_button:4;
};

void setActiveJoystick(unsigned int no, RefPtr<Joystick> joystick,
	JoystickSettings settings);
void setActiveMouse(RefPtr<Mouse> mouse,
	MouseSettings settings);

void setup();
}
