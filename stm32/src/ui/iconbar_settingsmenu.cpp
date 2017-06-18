
#include "iconbar_settingsmenu.hpp"
#include "videosettings.hpp"
#include "meminfo.hpp"

static ui::VideoSettings iconbar_videosettings;
static ui::MemInfo iconbar_meminfo;

IconBar_SettingsMenu::IconBar_SettingsMenu(ui::Control *iconbar_control)
  : iconbar_control(iconbar_control)
  {
}

void IconBar_SettingsMenu::selectItem(int index) {
  if(index == -1) {  // menu was dismissed, do nothing
    setVisible(false);
    UI_setTopLevelControl(iconbar_control);
  }
  if (index == 0) {
    setVisible(false);
    iconbar_videosettings.setVisible(true);
    settingsClosedCon = iconbar_videosettings.onClose().connect(sigc::mem_fun(this, &IconBar_SettingsMenu::settingsClosed));
    UI_setTopLevelControl(&iconbar_videosettings);
  }
  if (index == 1) {
    setVisible(false);
    iconbar_meminfo.setVisible(true);
    settingsClosedCon = iconbar_meminfo.onClose().connect(sigc::mem_fun(this, &IconBar_SettingsMenu::settingsClosed));
    UI_setTopLevelControl(&iconbar_meminfo);
  }
}

void IconBar_SettingsMenu::settingsClosed() {
  setVisible(false);
  iconbar_videosettings.setVisible(false);
  iconbar_meminfo.setVisible(false);
  UI_setTopLevelControl(iconbar_control);
  settingsClosedCon.disconnect();
}

