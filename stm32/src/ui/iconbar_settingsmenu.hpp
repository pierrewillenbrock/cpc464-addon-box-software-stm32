
#pragma once

#include "menu.hpp"

class IconBar_SettingsMenu : public ui::Menu {
public:
  ui::Control *iconbar_control;
  /*
        > Display settings
        > Memory Info
   */
  virtual unsigned int getItemCount() {
    return 2;
  }
  virtual std::string getItemText(unsigned int index) {
    if(index == 0)
      return "Display settings";
    else
      return "Memory Info";
  }
  sigc::connection settingsClosedCon;
  virtual void selectItem(int index);
  virtual void settingsClosed();
  IconBar_SettingsMenu(ui::Control *iconbar_control);
};


