
#pragma once

#include <ui/ui.hpp>
#include <fpga/sprite.hpp>
#include "controls.hpp"
#include "frame.hpp"
#include "listbox.hpp"
#include "input.hpp"
#include <ui/icons.hpp>

#include <string>
#include <vector>

namespace ui {
  class FileSelect : public ui::Frame, public sigc::trackable {
  private:
    Input m_folderinput;
    Input m_filename;
    ListBox m_names;
    Label m_folder_label;
    Label m_filename_label;
    Button m_up;
    Button m_newDir;
    Button m_action;
    Button m_cancel;
    std::string m_folder;
    enum {Idle, Reading, NeedsReread } readerState;
    struct FileInfo {
      bool dir;
      std::string name;
    };
    std::vector<FileInfo> m_files;
    sigc::signal<void,std::string> m_onFileSelected;
    sigc::signal<void> m_onCancel;
    RefPtr<Icon const> m_folderIcon;
    RefPtr<Icon const> m_newFolderIcon;
    RefPtr<Icon const> m_FileIcon;
    void actionClicked();
    void cancelClicked();
    void upClicked();
    void newDirClicked();
    void itemDblClicked(int item);
    void itemSelected(int item);
    void folderReader(std::string);
  public:
    FileSelect();
    ~FileSelect();
    void setActionText(std::string const &text);
    void setFolder(std::string const &folder);
    sigc::signal<void,std::string> &onFileSelected() { return m_onFileSelected; }
    sigc::signal<void> &onCancel() { return m_onCancel; }
    virtual void setVisible(bool visible);
  };
}
