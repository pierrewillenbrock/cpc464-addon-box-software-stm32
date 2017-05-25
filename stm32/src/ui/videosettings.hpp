
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
  class VideoSettings : public ui::Frame, public sigc::trackable {
  private:
    Input m_hsyncStart;
    Input m_hsyncEnd;
    Input m_hblankStart;
    Input m_hblankEnd;
    Input m_vsyncStart;
    Input m_vsyncEnd;
    Input m_vblankStart;
    Input m_vblankEnd;
    Label m_start;
    Label m_end;
    Label m_hsync;
    Label m_hblank;
    Label m_vsync;
    Label m_vblank;
    Button m_closeButton;
    sigc::signal<void> m_onClose;
    void closeClicked();
    void hsyncStartChanged(int value);
    void hsyncEndChanged(int value);
    void hblankStartChanged(int value);
    void hblankEndChanged(int value);
    void vsyncStartChanged(int value);
    void vsyncEndChanged(int value);
    void vblankStartChanged(int value);
    void vblankEndChanged(int value);
  public:
    VideoSettings();
    ~VideoSettings();
    sigc::signal<void> &onClose() { return m_onClose; }
    virtual void setVisible(bool visible);
  };
}
