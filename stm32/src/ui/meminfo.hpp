
#pragma once

#include <ui/ui.hpp>
#include <fpga/fpga_uploader.hpp>
#include "controls.hpp"
#include "frame.hpp"
#include "input.hpp"
#include <ui/icons.hpp>

#include <string>
#include <vector>

namespace ui {
  class MemInfo : public ui::Frame, public sigc::trackable {
  private:
    struct InfoLine {
      Label label;
      Input input;
      InfoLine() {}
    };
    std::array<InfoLine,14> infolines;
    Button m_closeButton;
    sigc::signal<void> m_onClose;
    void closeClicked();
  public:
    MemInfo();
    ~MemInfo();
    sigc::signal<void> &onClose() { return m_onClose; }
    virtual void setVisible(bool visible);
  };
}

// kate: indent-width 2; indent-mode cstyle;
