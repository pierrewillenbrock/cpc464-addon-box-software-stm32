
#pragma once

#include "menu.hpp"
#include <joystick.hpp>
#include <joyport.hpp>

class IconBar_JoystickMenu : public ui::Menu {
public:
  ui::Control *iconbar_control;
  unsigned joystickno;
  std::vector<RefPtr<Joystick> > joystick_available;
  /*
        >  Joystick #1
        > >Joystick #2
        > Activate
        > Settings
   */
  virtual unsigned int getItemCount();
  virtual std::string getItemText(unsigned int index);
  virtual void selectItem(int index);
  sigc::connection settingsSetCon;
  IconBar_JoystickMenu(ui::Control *iconbar_control);
  void setSettings(JoystickSettings settings);
};

