
#include "iconbar_joystickmenu.hpp"

#include <ui/iconbar.h>
#include <lang.hpp>

static RefPtr<Joystick> joystick_assigned[2];
static bool joystick_active[2];
static JoystickSettings joystick_settings[2] = {
  { 3, 4, 5, 0, 1, 2, 63 },
  { 3, 4, 5, 0, 1, 2, 63 },
};


IconBar_JoystickMenu::IconBar_JoystickMenu(ui::Control *iconbar_control)
  : iconbar_control(iconbar_control)
{
}

unsigned int IconBar_JoystickMenu::getItemCount() {
  if (joystickno >= 2)
    return 0;
  if (joystick_available.empty())
    return 1;
  if (joystick_assigned[joystickno])
    return joystick_available.size() + 2;
  else
    return joystick_available.size();
}

std::string IconBar_JoystickMenu::getItemText(unsigned int index) {
  if (joystickno >= 2)
    return "";
  if (joystick_available.empty())
    return "No joystick connected";
  if (index < joystick_available.size()) {
    if(joystick_assigned[joystickno] == joystick_available[index])
      return std::string(">") + lang::elide(joystick_available[index]->getName(), 15);
    else
      return std::string(" ") + lang::elide(joystick_available[index]->getName(), 15);
  }
  if(joystick_assigned[joystickno]) {
    if(index == joystick_available.size()) {
      if (joystick_active[joystickno])
        return "Deactivate";
      else
        return "Activate";
    }
    if(index == joystick_available.size()+1) {
      return "Settings";
    }
  }
  return "";
}

void IconBar_JoystickMenu::selectItem(int index) {
  if (joystickno >= 2)
    return;
  if (index == -1) { // menu was dismissed, do nothing
    setVisible(false);
    UI_setTopLevelControl(iconbar_control);
  }
  if (joystick_available.empty()) {
    setVisible(false);
    UI_setTopLevelControl(iconbar_control);
  }
  if((unsigned)index < joystick_available.size()) {
    if(joystick_assigned[joystickno] == joystick_available[index]) {
      joystick_assigned[joystickno] = RefPtr<Joystick>();
      joystick_active[joystickno] = false;
      joyport::setActiveJoystick(joystickno, RefPtr<Joystick>(),
                                 joystick_settings[joystickno]);
      IconBar_joystick_unassigned(joystickno);
      setVisible(false);
      UI_setTopLevelControl(iconbar_control);
    } else {
      joystick_active[joystickno] = true;
      joystick_assigned[joystickno] = joystick_available[index];

      ///\todo open the settings dlg
      joyport::setActiveJoystick(joystickno, joystick_available[index],
                                 joystick_settings[joystickno]);
      IconBar_joystick_assigned(joystickno, joystick_available[index]->getName().c_str());
      setVisible(false);
      UI_setTopLevelControl(iconbar_control);

    }
  }
  if(joystick_assigned[joystickno]) {
    if((unsigned)index == joystick_available.size()) {
      setVisible(false);
      UI_setTopLevelControl(iconbar_control);
      if(joystick_active[joystickno]) {
        joystick_active[joystickno] = false;
        joyport::setActiveJoystick(joystickno, RefPtr<Joystick>(),
                                   joystick_settings[joystickno]);
	IconBar_joystick_unassigned(joystickno);
      } else {
        joystick_active[joystickno] = true;
        joyport::setActiveJoystick(joystickno, joystick_assigned[joystickno],
                                   joystick_settings[joystickno]);
        IconBar_joystick_assigned(joystickno, joystick_available[index]->getName().c_str());
      }
    }
    if((unsigned)index == joystick_available.size()+1) {
      ///\todo open settings dlg
      ///\todo wire up some method to notify about joystick unassign through joyport mode switch
      setVisible(false);
      UI_setTopLevelControl(iconbar_control);
    }
  }
}

void IconBar_JoystickMenu::setSettings(JoystickSettings settings) {
  if(joystickno >= 2) {
    UI_setTopLevelControl(iconbar_control);
    return;
  }
  joystick_settings[joystickno] = settings;
  if(joystick_assigned[joystickno] && joystick_active[joystickno]) {
    joyport::setActiveJoystick(joystickno, joystick_assigned[joystickno],
                               joystick_settings[joystickno]);
  }
  UI_setTopLevelControl(iconbar_control);
}


// kate: indent-width 2; indent-mode cstyle;
