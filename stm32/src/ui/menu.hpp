
#pragma once

#include <ui/ui.hpp>
#include <fpga/sprite.hpp>
#include <fpga/fpga_uploader.hpp>

#include <string>
#include <vector>

namespace ui {
  class Menu : public ui::MappedControl {
  private:
    bool m_visible;
    Point m_position;
    MappedSprite m_sprite;
    int highlight_item;
    int mouse_pressed_item;
    //also updates sprites hsize/vsize
    void generateMap();
  protected:
    virtual uint32_t const &map(unsigned x, unsigned y) const {
	    return m_sprite.at(x,y);
    }
    virtual uint32_t &map(unsigned x, unsigned y) {
	    return m_sprite.at(x,y);
    }
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
    virtual void mouseMove(int16_t /*dx*/, int16_t /*dy*/, MouseState mousestate);
    virtual void joyTrgDown(ui::JoyTrg trg, ui::JoyState state);
    virtual void joyTrgUp(ui::JoyTrg trg, ui::JoyState state);
    virtual unsigned int getItemCount() = 0;
    virtual std::string getItemText(unsigned int index) = 0;
    virtual void selectItem(int index) = 0;
  };
}
