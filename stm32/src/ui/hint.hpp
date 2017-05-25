
#pragma once

#include <ui/ui.hpp>
#include <fpga/sprite.hpp>
#include <fpga/fpga_uploader.hpp>

#include <string>
#include <vector>

namespace ui {
  class Hint {
  private:
    std::string m_text;
    bool m_visible;
    MappedSprite m_sprite;
  public:
    Hint();
    ~Hint();
    void setPosition(Point p);
    void setText(std::string const &text);
    void setVisible(bool visible);
  };
}

// kate: indent-width 2; indent-mode cstyle; replace-tabs on;
