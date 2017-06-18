
#include <joyport.hpp>
#include <fpga/fpga_uploader.hpp>
#include <fpga/layout.h>

using namespace joyport;

static RefPtr<Joystick> joyport_joystick[2];
static sigc::connection joyport_joystickChangeCon[2];
static JoystickSettings joyport_joystickSettings[2];
static RefPtr<Mouse> joyport_mouse;
static sigc::connection joyport_mouseChangeCon;
static MouseSettings joyport_mouseSettings;
static uint8_t joystick_state[2];
static int8_t mouse_inc[2];
static FPGA_Uploader joyport_joystick_uploader;
static FPGA_Uploader joyport_mouse_uploader;


static void Joyport_JoystickHandler(JoystickEvent e, unsigned int no) {
	//we just take the event and translate that to the relevant
	uint8_t new_state = 0;
	//x is right positive
	if(e.axis[0] > joyport_joystickSettings[no].axis_threshold)
		new_state |= FPGA_JOYSTICK_JST_RIGHT;
	if(e.axis[0] < -joyport_joystickSettings[no].axis_threshold)
		new_state |= FPGA_JOYSTICK_JST_LEFT;
	//y is down positive
	if(e.axis[1] < -joyport_joystickSettings[no].axis_threshold)
		new_state |= FPGA_JOYSTICK_JST_UP;
	if(e.axis[1] > joyport_joystickSettings[no].axis_threshold)
		new_state |= FPGA_JOYSTICK_JST_DOWN;
	///\todo: make sure the buttons are mapped correctly.
	if(e.buttons & (1 << joyport_joystickSettings[no].fire1_button))
		new_state |= FPGA_JOYSTICK_JST_FIRE1;
	if(e.buttons & (1 << joyport_joystickSettings[no].fire2_button))
		new_state |= FPGA_JOYSTICK_JST_FIRE2;
	if(e.buttons & (1 << joyport_joystickSettings[no].spare_button))
		new_state |= FPGA_JOYSTICK_JST_SPARE;

	if(joystick_state[no] != new_state) {
		joystick_state[no] = new_state;
		joyport_joystick_uploader.triggerUpload();
	}

	if(e.buttons & (1 << joyport_joystickSettings[no].mode_toggle_button)) {
		joyport_joystickChangeCon[no].disconnect();
		joyport_joystick[no]->setExclusive(false);
		joyport_joystick[no] = RefPtr<Joystick>();
	}
}

static void Joyport_MouseHandler(MouseEvent e) {
	//we just take the event and translate that to the relevant
	//fpga control bytes
	//fpga addresses:
	//FPGA_JOYSTICK_J1ST
	//FPGA_JOYSTICK_J2ST
	uint8_t new_state = joystick_state[0] &
	                    ~(FPGA_JOYSTICK_JST_FIRE1 | FPGA_JOYSTICK_JST_FIRE2);
	if(e.buttons & 0x1)
		new_state |= FPGA_JOYSTICK_JST_FIRE1;
	if(e.buttons & 0x2)
		new_state |= FPGA_JOYSTICK_JST_FIRE2;
	if (joystick_state[0] != new_state) {
		joystick_state[0] = new_state;
		joyport_joystick_uploader.triggerUpload();
	}

	if(e.buttons & (1 << joyport_mouseSettings.mode_toggle_button)) {
		joyport_mouseChangeCon.disconnect();
		joyport_mouse->setExclusive(false);
		joyport_mouse = RefPtr<Mouse>();
	}

	if(e.delta_axis[0] || e.delta_axis[1]) {
		mouse_inc[0] = e.delta_axis[0];
		mouse_inc[1] = e.delta_axis[1];

		joyport_mouse_uploader.triggerUpload();
	}
}

void joyport::setActiveJoystick(unsigned int no, RefPtr<Joystick> joystick,
	JoystickSettings settings) {
	if (no > 1)
		return;
	if (joyport_joystick[no]) {
		joyport_joystickChangeCon[no].disconnect();
		joyport_joystick[no]->setExclusive(false);
	}
	joyport_joystick[no] = joystick;
	joyport_joystickSettings[no] = settings;
	if(joyport_joystick[no]) {
		joyport_joystickChangeCon[no] =
		        joyport_joystick[no]->onJoystickChange().connect(
		                sigc::bind(
		                        sigc::ptr_fun(Joyport_JoystickHandler),
		                        no));
		joyport_joystick[no]->setExclusive(true);
	}
}

void joyport::setActiveMouse(RefPtr<Mouse> mouse,
	MouseSettings settings) {
	if (joyport_mouse) {
		joyport_mouseChangeCon.disconnect();
		joyport_mouse->setExclusive(false);
	}
	joyport_mouse = mouse;
	joyport_mouseSettings = settings;
	if(joyport_mouse) {
		joyport_mouseChangeCon =
		        joyport_mouse->onMouseChange().connect(
		                sigc::ptr_fun(Joyport_MouseHandler));
		joyport_mouse->setExclusive(true);
	}
}

void joyport::setup() {
	joyport_joystick_uploader.setDest(FPGA_JOYSTICK_J1ST);
	joyport_joystick_uploader.setSize(2);
	joyport_joystick_uploader.setSrc(joystick_state);
	joyport_mouse_uploader.setDest(FPGA_JOYSTICK_MOUSEXINC);
	joyport_mouse_uploader.setSize(2);
	joyport_mouse_uploader.setSrc(mouse_inc);
}
