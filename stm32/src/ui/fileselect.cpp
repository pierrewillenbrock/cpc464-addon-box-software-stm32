
#include "fileselect.hpp"

#include <unistd.h>
#include <dirent.h>

#include <deferredwork.hpp>

using namespace ui;

/* layout:
   +---------------------------------------------------------------------------
   ! Folder: #######Folder input, full path###################### ##up button##
   !
   ! ##########################################################################
   ! #########names+icons of files in current dir##############################
   ! ##########################################################################
   !
   ! Filename: #########name input field#######################################
   !
   !                                              #save button# #cancel button#
   +---------------------------------------------------------------------------

   we need: * input box as subelement on a sprite
            * list box as subelement on a sprite
            * scroll bar as subelement on a sprite
            * button as subelement on a sprite


   buttons look like this: "[ text ]", with the same bg/fg as the rest
   inputs and list box have alternate fg/bg. current selection or
     cursor position uses regular bg/fg
   scroll bar uses "< >" or "v ^" and an alternate pal ' ' for position marker.
     position marker grows as usual.
 */


FileSelect::FileSelect()
  : m_folderinput(this)
  , m_filename(this)
  , m_names(this)
  , m_folder_label(this)
  , m_filename_label(this)
  , m_up(this)
  , m_newDir(this)
  , m_action(this)
  , m_cancel(this)
{
  addChild(&m_folderinput);
  addChild(&m_filename);
  addChild(&m_names);
  addChild(&m_folder_label);
  addChild(&m_filename_label);
  addChild(&m_up);
  addChild(&m_newDir);
  addChild(&m_action);
  addChild(&m_cancel);
  m_folder_label.setText("Folder:");
  m_filename_label.setText("Filename:");
  m_up.setText("^");
  m_cancel.setText("Cancel");
  //max size: 98x71, that'd take 6958 bytes(18 bit) in vram.
  //we can spare 2048-64-24*16-19*16=1296 bytes, minus a few bytes here and
  //there. safe bet would probably be 1000 bytes. and we must remove our crap
  //if invisible.
  unsigned w = 40;
  unsigned h = 24;
  setPosition(Point(screen.rect().x + (screen.rect().width - w*8)/2,
		    screen.rect().y + (screen.rect().height - h*8)/2));
  setSize(w,h);
  m_folder_label.setPosition(0,0);
  m_folder_label.setSize(9,1);
  m_folderinput.setPosition(9,0);
  m_folderinput.setSize(w-15,1);
  m_up.setPosition(w-6,0);
  m_up.setSize(3,1);
  m_newDir.setPosition(w-3,0);
  m_newDir.setSize(3,1);

  m_names.setPosition(0,1);
  m_names.setSize(w,h-3);

  m_filename_label.setPosition(0,h-2);
  m_filename_label.setSize(9,1);
  m_filename.setPosition(9,h-2);
  m_filename.setSize(w-9,1);

  m_action.setPosition(w-10,h-1);
  m_action.setSize(2,1);
  m_cancel.setPosition(w-8,h-1);
  m_cancel.setSize(8,1);

  m_folder_label.setVisible(true);
  m_folderinput.setVisible(true);
  m_newDir.setVisible(true);
  m_up.setVisible(true);
  m_filename_label.setVisible(true);
  m_filename.setVisible(true);
  m_cancel.setVisible(true);
  m_action.onClick().connect(sigc::mem_fun(this, &FileSelect::actionClicked));
  m_cancel.onClick().connect(sigc::mem_fun(this, &FileSelect::cancelClicked));
  m_up.onClick().connect(sigc::mem_fun(this, &FileSelect::upClicked));
  m_newDir.onClick().connect(sigc::mem_fun(this, &FileSelect::newDirClicked));
  m_names.onSelected().connect(sigc::mem_fun(this, &FileSelect::itemSelected));
  m_names.onDblClick().connect(sigc::mem_fun(this, &FileSelect::itemDblClicked));
}

FileSelect::~FileSelect() {
}

void FileSelect::itemDblClicked(int item) {
  if (item == -1)
    return;
  if ((unsigned)item >= m_files.size())
    return;
  std::string f = m_folder;
  if (f != "/")
    f += "/";
  f += m_files[item].name;
  if (m_files[item].dir) {
    setFolder(f);
  } else {
    m_onFileSelected(f);
  }
}

void FileSelect::itemSelected(int item) {
  if (item == -1)
    return;
  if ((unsigned)item >= m_files.size())
    return;
  m_filename.setText(m_files[item].name);
}

void FileSelect::actionClicked() {
  //check if the current item in m_filename is a directory. if so,
  //reload that one. otherwise, report back that we have a name.
  std::string f = m_folder;
  if (f != "/")
    f += "/";
  f += m_filename.text();
  std::string const &name = m_filename.text();
  for(auto &fi : m_files) {
    if (fi.name == name) {
      //found it.
      if (fi.dir) {
	setFolder(f);
	return;
      }
      break;
    }
  }
  m_onFileSelected(f);
}

void FileSelect::cancelClicked() {
  m_onCancel();
}

void FileSelect::upClicked() {
  std::string f = m_folder;
  int i;
  for(i = f.size()-1; i >= 0; i--) {
    if (f[i] == '/')
      break;
  }
  if (i < 0)
    return;
  if (i == 0) {
    setFolder("/");
    return;
  }
  f.resize(i);
  setFolder(f);
}

void FileSelect::newDirClicked() {
  //todo, we don't have character input, yet...
}

void FileSelect::folderReader(std::string folder) {
  {
    ISR_Guard g;
    if (folder != m_folder)
      return;
    readerState = Reading;
  }
  DIR *dir = opendir(folder.c_str());
  if (!dir) {
    ISR_Guard g;
    readerState = Idle;
    m_names.clearItems();
    return;
  }
  struct dirent *dent;
  std::vector<FileInfo> files;
  while((dent = readdir(dir))) {
    if((dent->d_name[0] == '.' && dent->d_name[1] == '\0') ||
       (dent->d_name[0] == '.' && dent->d_name[1] == '.' && dent->d_name[2] == '\0'))
      continue;
    FileInfo fi;
    fi.dir = dent->d_type == DT_DIR;
    fi.name = dent->d_name;
    files.push_back(fi);
    {
      ISR_Guard g;
      if (readerState != Reading)
	break;
    }
  }
  closedir(dir);
  {
    ISR_Guard g;
    if (readerState == Reading &&
	folder == m_folder) {
      m_files = files;
      if (visible()) {
	m_names.clearItems();
	for(auto &fi : m_files) {
	  ListBox::Item it;
	  if (fi.dir)
	    it.icon = icons.getIcon(Icons::Folder);
	  else
	    it.icon = icons.getIcon(Icons::File);
	  it.text = fi.name;
	  m_names.addItem(it);
	}
	m_names.setVisible(true);
      }
      readerState = Idle;
    }
  }
}

void FileSelect::setActionText(std::string const &text) {
  m_action.setText(text);
  m_action.setVisible(true);
  m_action.setPosition(width()-10-text.size(),height()-1);
  m_action.setSize(2+text.size(),1);
}

void FileSelect::setFolder(std::string const &folder) {
  if (m_folder == folder)
    return;
  m_folder = folder;
  m_folderinput.setText(folder);
  ISR_Guard g;
  if (readerState != Idle) {
    readerState = NeedsReread;
  }
  addDeferredWork(sigc::bind(sigc::mem_fun(this, &FileSelect::folderReader), folder));
}

void FileSelect::setVisible(bool visible) {
  Frame::setVisible(visible);
  if (visible) {
    unsigned w = 40;
    unsigned h = 24;
    setPosition(Point(screen.rect().x + (screen.rect().width - w*8)/2,
		      screen.rect().y + (screen.rect().height - h*8)/2));
    setSize(w,h);
    m_newDir.setIcon(icons.getIcon(Icons::NewFolder));
    ISR_Guard g;
    m_names.clearItems();
    for(auto &fi : m_files) {
      ListBox::Item it;
      if (fi.dir)
	it.icon = icons.getIcon(Icons::Folder);
      else
	it.icon = icons.getIcon(Icons::File);
      it.text = fi.name;
      m_names.addItem(it);
    }
    m_names.setVisible(true);
  } else {
    m_newDir.setIcon(0);
    m_names.clearItems();
  }
}
