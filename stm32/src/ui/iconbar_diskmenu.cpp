
#include "iconbar_diskmenu.hpp"

#include <fdc/fdc.h>
#include <ui/iconbar.h>
#include <deferredwork.hpp>
#include "fileselect.hpp"

static std::string iconbar_recent_disks[4];
static std::string iconbar_current_disks[4];

static void IconBar_addRecent(std::string const &image) {
  std::string t = image;
  for(unsigned int i = 0; i < 4; i++) {
    std::string t2 = iconbar_recent_disks[i];
    iconbar_recent_disks[i] = t;
    t = t2;
    if (t == image)
      break;
  }
}

static void IconBar_DeferredEjectDisk(unsigned drive) {
  FDC_EjectDisk(drive);
  IconBar_disk_unassigned(drive);
  IconBar_addRecent(iconbar_current_disks[drive]);
}

static void IconBar_DeferredOpenDisk(unsigned drive, std::string file) {
  FDC_EjectDisk(drive);
  FDC_InsertDisk(drive,file.c_str());
  IconBar_disk_assigned(drive, file.c_str());
  iconbar_current_disks[drive] = file;
}

static void IconBar_DeferredCreateDisk(unsigned drive, std::string file) {
  FDC_EjectDisk(drive);
  //todo
  //FDC_InsertDisk(drive,file.c_str());
  IconBar_disk_assigned(drive, file.c_str());
  iconbar_current_disks[drive] = file;
}

IconBar_DiskMenu::IconBar_DiskMenu(ui::Control *iconbar_control,
	  ui::FileSelect *iconbar_fileselect)
  : iconbar_control(iconbar_control)
  , iconbar_fileselect(iconbar_fileselect)
{
}

unsigned int IconBar_DiskMenu::getItemCount() {
  if(disk_assigned)
    return 1;
  unsigned int num = 0;
  for(unsigned int i = 0; i < 4; i++) {
    if(!iconbar_recent_disks[i].empty())
      num++;
  }
  return num+2;
}

std::string IconBar_DiskMenu::getItemText(unsigned int index) {
  if(disk_assigned)
    return "Eject Disk";
  else
    switch(index) {
    case 0: return "Insert Disk...";
    case 1: return "Create Disk...";
    case 2:
    case 3:
    case 4:
    case 5: return iconbar_recent_disks[index-2];
    default: return "";
    }
}

void IconBar_DiskMenu::selectItem(int index) {
  if (index == -1) { // menu was dismissed, do nothing
    setVisible(false);
    UI_setTopLevelControl(iconbar_control);
  }
  if (disk_assigned) {
    if (index == 0) {
      addDeferredWork(sigc::bind(sigc::ptr_fun(IconBar_DeferredEjectDisk),diskno));
    }
    UI_setTopLevelControl(iconbar_control);
    setVisible(false);
  } else {
    if (index == 0) { // insert disk.
      setVisible(false);
      iconbar_fileselect->setVisible(true);
      iconbar_fileselect->setActionText("Open");
      fileSelectedCon = iconbar_fileselect->onFileSelected().connect(sigc::mem_fun(this, &IconBar_DiskMenu::fileSelectedOpen));
      fileSelectCanceledCon = iconbar_fileselect->onCancel().connect(sigc::mem_fun(this, &IconBar_DiskMenu::fileSelectCanceled));
      UI_setTopLevelControl(iconbar_fileselect);
    } else if (index == 1) { // new disk. todo: need to add restrictions to file chooser: accept directories(using action), accept existing files, accept only existing files, accept only non-existing files
      setVisible(false);
      iconbar_fileselect->setVisible(true);
      iconbar_fileselect->setActionText("Create");
      fileSelectedCon = iconbar_fileselect->onFileSelected().connect(sigc::mem_fun(this, &IconBar_DiskMenu::fileSelectedCreate));
      fileSelectCanceledCon = iconbar_fileselect->onCancel().connect(sigc::mem_fun(this, &IconBar_DiskMenu::fileSelectCanceled));
      UI_setTopLevelControl(iconbar_fileselect);
    } else if (index > 2+4) {
      setVisible(false);
      UI_setTopLevelControl(iconbar_control);
    } else if (iconbar_recent_disks[index - 2].empty()) {
      setVisible(false);
      UI_setTopLevelControl(iconbar_control);
    } else {
      setVisible(false);
      UI_setTopLevelControl(iconbar_control);
      addDeferredWork(sigc::bind(sigc::bind(sigc::ptr_fun(IconBar_DeferredOpenDisk),iconbar_recent_disks[index - 2]),diskno));
    }
  }
}

void IconBar_DiskMenu::fileSelectedOpen(std::string file) {
  iconbar_fileselect->setVisible(false);
  UI_setTopLevelControl(iconbar_control);
  addDeferredWork(sigc::bind(sigc::bind(sigc::ptr_fun(IconBar_DeferredOpenDisk),file),diskno));
  fileSelectedCon.disconnect();
  fileSelectCanceledCon.disconnect();
}

void IconBar_DiskMenu::fileSelectedCreate(std::string file) {
  iconbar_fileselect->setVisible(false);
  UI_setTopLevelControl(iconbar_control);
  addDeferredWork(sigc::bind(sigc::bind(sigc::ptr_fun(IconBar_DeferredCreateDisk),file),diskno));
  fileSelectedCon.disconnect();
  fileSelectCanceledCon.disconnect();
}

void IconBar_DiskMenu::fileSelectCanceled() {
  iconbar_fileselect->setVisible(false);
  UI_setTopLevelControl(iconbar_control);
  fileSelectedCon.disconnect();
  fileSelectCanceledCon.disconnect();
}
