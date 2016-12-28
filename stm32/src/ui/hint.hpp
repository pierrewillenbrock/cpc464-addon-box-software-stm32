
#pragma once

#include <ui/ui.hpp>
#include <fpga/sprite.hpp>

#include <string>
#include <vector>

namespace ui {
  class Hint {
  private:
    std::string m_text;
    bool m_visible;
    sprite_info m_spriteinfo;
    std::vector<uint32_t> map;
    Sprite m_sprite;
    FPGA_Uploader map_uploader;
  public:
    Hint();
    ~Hint();
    void setPosition(Point p);
    void setText(std::string const &text);
    void setVisible(bool visible);
  };
}
