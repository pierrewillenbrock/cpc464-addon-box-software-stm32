
#pragma once

#include "menu.hpp"

namespace ui {
class FileSelect;
}

class IconBar_DiskMenu : public ui::Menu {
public:
  ui::Control *iconbar_control;
  ui::FileSelect *iconbar_fileselect;
  unsigned diskno;
  bool disk_assigned;
  /*
        > insert disk (if none inserted)
        > new disk (if none inserted)
        > eject disk (if inserted)
	> ----
        > last 4 inserted disks  (if none inserted)
   */
  virtual unsigned int getItemCount();
  virtual std::string getItemText(unsigned int index);
  sigc::connection fileSelectedCon;
  sigc::connection fileSelectCanceledCon;
  virtual void selectItem(int index);
  void fileSelectedOpen(std::string file);
  void fileSelectedCreate(std::string file);
  void fileSelectCanceled();
  IconBar_DiskMenu(ui::Control *iconbar_control,
	  ui::FileSelect *iconbar_fileselect);
};

