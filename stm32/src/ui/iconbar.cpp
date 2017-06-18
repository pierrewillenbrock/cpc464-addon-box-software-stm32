
#include <ui/iconbar.h>
#include <ui/ui.hpp>
#include "hint.hpp"
#include "menu.hpp"
#include "fileselect.hpp"

#include <fpga/sprite.hpp>
#include <fpga/fpga_comm.h>
#include <fpga/layout.h>
#include <fpga/fpga_uploader.hpp>
#include <timer.h>
#include <deferredwork.hpp>

#include <sstream>

#include "iconbar_control.hpp"
#include "iconbar_diskmenu.hpp"
#include "iconbar_settingsmenu.hpp"
#include "iconbar_joystickmenu.hpp"

static bool iconbar_disk_assigned[4];

class IconBar_ControlImpl : public IconBar_Control {
public:
  virtual void diskSelected(unsigned diskno, ui::Point where);
  virtual void joystickSelected(unsigned joyno, ui::Point where);
  virtual void mouseSelected(ui::Point where);
  virtual void lpenSelected(ui::Point where);
  virtual void settingsSelected(ui::Point where);
};

static IconBar_ControlImpl iconbar_control;
static ui::FileSelect iconbar_fileselect;

static IconBar_DiskMenu iconbar_diskmenu(&iconbar_control,
    &iconbar_fileselect);
static IconBar_SettingsMenu iconbar_settingsmenu(&iconbar_control);
static IconBar_JoystickMenu iconbar_joystickmenu(&iconbar_control);

void IconBar_ControlImpl::diskSelected(unsigned diskno, ui::Point where) {
  iconbar_diskmenu.diskno = diskno;
  iconbar_diskmenu.disk_assigned = iconbar_disk_assigned[diskno];
  iconbar_diskmenu.setPosition(where);
  iconbar_diskmenu.setVisible(true);
  UI_setTopLevelControl(&iconbar_diskmenu);
}

void IconBar_ControlImpl::joystickSelected(unsigned joyno, ui::Point where) {
  iconbar_joystickmenu.joystickno = joyno;
  iconbar_joystickmenu.joystick_available = Joystick_get();
  iconbar_joystickmenu.setPosition(where);
  iconbar_joystickmenu.setVisible(true);
  UI_setTopLevelControl(&iconbar_joystickmenu);
}

void IconBar_ControlImpl::mouseSelected(ui::Point /*where*/) {
}

void IconBar_ControlImpl::lpenSelected(ui::Point /*where*/) {
}

void IconBar_ControlImpl::settingsSelected(ui::Point where) {
  iconbar_settingsmenu.setPosition(where);
  iconbar_settingsmenu.setVisible(true);
  UI_setTopLevelControl(&iconbar_settingsmenu);
}

void IconBar_disk_assigned(unsigned no, char const *displayname) {
  if (no >= 4)
    return;
  iconbar_disk_assigned[no] = true;

  std::stringstream ss;
  ss << "Disk drive " << (char)(no+'A') << ": "
     << displayname;

  iconbar_control.setDiskHint(no, ss.str());
  iconbar_control.setDiskAssigned(no, true);
}

void IconBar_disk_unassigned(unsigned no) {
  if (no >= 4)
    return;
  iconbar_disk_assigned[no] = false;

  std::stringstream ss;
  ss << "Disk drive " << (char)(no+'A') << ": not assigned";

  iconbar_control.setDiskHint(no, ss.str());
  iconbar_control.setDiskAssigned(no, false);
}

void IconBar_disk_activity(unsigned no, bool activity) {
  iconbar_control.setDiskActivity(no, activity);
}

void IconBar_disk_motor_on() {
  iconbar_control.setDiskMotor(true);
}

void IconBar_disk_motor_off() {
  iconbar_control.setDiskMotor(false);
}

void IconBar_joystick_assigned(unsigned no, char const *displayname) {
  if (no >= 2)
    return;

  std::stringstream ss;
  ss << "Joystick " << (no+1) << ": " << displayname;

  iconbar_control.setJoystickHint(no,ss.str());
  iconbar_control.setJoystickAssigned(no, true);
}

void IconBar_joystick_unassigned(unsigned no) {
  if (no >= 2)
    return;

  std::stringstream ss;
  ss << "Joystick " << (no+1) << ": not assigned";

  iconbar_control.setJoystickHint(no,ss.str());
  iconbar_control.setJoystickAssigned(no, false);
}

void IconBar_mouse_assigned(char const *displayname) {
  std::stringstream ss;
  ss << "Mouse: " << displayname;

  iconbar_control.setMouseHint(ss.str());
  iconbar_control.setMouseAssigned(true);
}

void IconBar_mouse_unassigned() {
  iconbar_control.setMouseHint("Mouse: not assigned");
  iconbar_control.setMouseAssigned(false);
}

void IconBar_lpen_assigned(char const *displayname) {
  std::stringstream ss;
      ss << "Light pen: " << displayname;

  iconbar_control.setLpenHint(ss.str());
  iconbar_control.setLpenAssigned(true);
}

void IconBar_lpen_unassigned() {
  iconbar_control.setLpenHint("Light pen: not assigned");
  iconbar_control.setLpenAssigned(false);
}

void IconBar_Setup() {
  iconbar_fileselect.setFolder("/media");

  iconbar_control.init();

  std::stringstream ss;
  for(unsigned i = 0; i < 4; i++) {
    ss.str("");
    ss << "Disk drive " << (char)(i+'A') << ": not assigned";
    iconbar_control.setDiskHint(i, ss.str());
  }
  for(unsigned i = 0; i < 2; i++) {
    ss.str("");
    ss << "Joystick " << (i+1) << ": not assigned";
    iconbar_control.setJoystickHint(i, ss.str());
  }
  iconbar_control.setMouseHint("Mouse: not assigned");
  iconbar_control.setLpenHint("Light pen: not assigned");

  UI_setTopLevelControl(&iconbar_control);
}

// kate: indent-width 2; indent-mode cstyle;
