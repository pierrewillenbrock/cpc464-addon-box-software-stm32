
#pragma once

#include <ui/ui.hpp>
#include <fpga/sprite.hpp>

#include <string>
#include <vector>

namespace ui {
  class Menu : public ui::Control {
  private:
    bool m_visible;
    Point m_position;
    sprite_info m_spriteinfo;
    std::vector<uint32_t> map;
    Sprite m_sprite;
    FPGA_Uploader map_uploader;
    int mouse_over_item;
    int pressed_item;
    //also updates sprites hsize/vsize
    void generateMap();
  public:
    Menu();
    ~Menu();
    //this position is the position of one of the corners.
    //the menu is moved if it would cross the screen edge.
    void setPosition(Point p);
    void setVisible(bool visible);
    virtual Rect getRect();
    virtual Rect getGlobalRect();
    virtual void mouseDown(uint8_t button, MouseState mousestate);
    virtual void mouseUp(uint8_t button, MouseState mousestate);
    virtual void mouseMove(int16_t dx, int16_t dy, MouseState mousestate);
    virtual unsigned int getItemCount() = 0;
    virtual std::string getItemText(unsigned int index) = 0;
    virtual void selectItem(int index) = 0;
  };
}
