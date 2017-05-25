
#include <fpga/font.h>
#include "menu.hpp"
#include <fpga/font.h>
#include <fpga/layout.h>

using namespace ui;

Menu::Menu() {
  m_sprite.setPriority(30);
  m_sprite.setZOrder(40);
  m_sprite.setDoubleSize(false);
  mouse_over_item = -1;
}

Menu::~Menu() {
}

//just the corner position. gets adjusted to fit on screen
void Menu::setPosition(Point p) {
  m_position = p;
  //need to generate the map to know its size and adjust position
  if (m_visible) {
    generateMap();
  }
}

void Menu::generateMap() {
  unsigned max_len = 0;
  unsigned h = getItemCount();
  for(unsigned i = 0; i < h; i++) {
    if (max_len < getItemText(i).size())
      max_len = getItemText(i).size();
  }
  unsigned w = max_len;
  m_sprite.setSize(w,h);
  for(unsigned i = 0; i < h; i++) {
    unsigned int pal = 15;
    if ((int)i == mouse_over_item) {
      pal = 11;
    }
    std::string text = getItemText(i);
    unsigned j = 0;
    for(; j < text.size(); j++)
      map(j, i) = font_get_tile(text[j],pal, 1);
    for(; j < max_len; j++)
      map(j, i) = font_get_tile(' ',pal, 1);
  }
  m_sprite.updateDone();
  unsigned x = m_position.x;
  unsigned y = m_position.y;
  if (x + w*8 >
      screen.rect().x+screen.rect().width)
    x = m_position.x-w*8;
  if (y + h*8 >
      screen.rect().y+screen.rect().height)
    y = m_position.y-h*8;
  m_sprite.setPosition(x,y);
}

Rect Menu::getRect() {
  sprite_info const &i = m_sprite.info();
  Rect r;
  r.x = i.hpos;
  r.y = i.vpos;
  r.width = i.hsize*8;
  r.height = i.vsize*8;
  return r;
}

Rect Menu::getGlobalRect() {
  return getRect();
}

void Menu::mouseDown(uint8_t button, MouseState mousestate) {
  ui::Rect r = getGlobalRect();
  if (mousestate.x < r.x ||
      mousestate.y < r.y ||
      mousestate.x >= r.x+r.width ||
      mousestate.y >= r.y+r.height) {
    if (button == 0 && mousestate.buttons == 1)
      pressed_item = -1;
    return;
  }
  if (button == 0 && mousestate.buttons == 1)
    pressed_item = (mousestate.y - r.y)/8;
}

void Menu::mouseUp(uint8_t button, MouseState mousestate) {
  ui::Rect r = getGlobalRect();
  if (mousestate.x < r.x ||
      mousestate.y < r.y ||
      mousestate.x >= r.x+r.width ||
      mousestate.y >= r.y+r.height) {
    if (button == 0 && mousestate.buttons == 0) {
      if (pressed_item == -1)
	selectItem(-1);
    }
    return;
  }
  if (button == 0 && mousestate.buttons == 0) {
    int itemno = (mousestate.y - r.y)/8;
    if (itemno == pressed_item) {
      selectItem(itemno);
    }
  }
}

void Menu::mouseMove(int16_t /*dx*/, int16_t /*dy*/, MouseState mousestate) {
  ui::Rect r = getRect();
  if (mousestate.x < r.x ||
      mousestate.y < r.y ||
      mousestate.x >= r.x+r.width ||
      mousestate.y >= r.y+r.height ||
      mousestate.buttons) {
    if (mouse_over_item != -1) {
      mouse_over_item = -1;
      generateMap();
    }
    return;
  }
  int itemno = (mousestate.y - r.y)/8;
  if (itemno != mouse_over_item) {
    mouse_over_item = itemno;
    generateMap();
  }
}

void Menu::setVisible(bool visible) {
  if (m_visible == visible)
    return;
  m_visible = visible;
  if (visible) {
    generateMap();
  }
  m_sprite.setVisible(m_visible);
}

// kate: indent-width 2; indent-mode cstyle;
