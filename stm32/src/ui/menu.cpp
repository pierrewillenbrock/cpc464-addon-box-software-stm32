
#include <fpga/font.h>
#include "controls.hpp"
#include "menu.hpp"
#include <fpga/font.h>
#include <fpga/layout.h>

using namespace ui;

Menu::Menu() {
  m_sprite.setPriority(30);
  m_sprite.setZOrder(40);
  m_sprite.setDoubleSize(false);
  highlight_item = -1;
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
    PaletteEntry pal = palette.menu;
    if ((int)i == highlight_item)
      pal = palette.menu_selected;
    std::string text = getItemText(i);
    unsigned j = 0;
    for(; j < text.size(); j++)
      map(j, i) = font_get_tile(text[j], pal.sel, pal.idx);
    for(; j < max_len; j++)
      map(j, i) = font_get_tile(' ', pal.sel, pal.idx);
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
      mouse_pressed_item = -1;
    return;
  }
  if (button == 0 && mousestate.buttons == 1)
    mouse_pressed_item = (mousestate.y - r.y)/8;
}

void Menu::mouseUp(uint8_t button, MouseState mousestate) {
  ui::Rect r = getGlobalRect();
  if (mousestate.x < r.x ||
      mousestate.y < r.y ||
      mousestate.x >= r.x+r.width ||
      mousestate.y >= r.y+r.height) {
    if (button == 0 && mousestate.buttons == 0) {
      if (mouse_pressed_item == -1)
	selectItem(-1);
    }
    return;
  }
  if (button == 0 && mousestate.buttons == 0) {
    int itemno = (mousestate.y - r.y)/8;
    if (itemno == mouse_pressed_item) {
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
    if (highlight_item != -1) {
      highlight_item = -1;
      generateMap();
    }
    return;
  }
  int itemno = (mousestate.y - r.y)/8;
  if (itemno != highlight_item) {
    highlight_item = itemno;
    generateMap();
  }
}

void Menu::joyTrgDown(ui::JoyTrg trg, ui::JoyState /*state*/) {
  switch(trg) {
  case ui::JoyTrg::Down:
    if (highlight_item == -1 ||
      (unsigned)highlight_item == getItemCount()-1)
      highlight_item = 0;
    else
      highlight_item++;
    generateMap();
    break;
  case ui::JoyTrg::Up:
    if (highlight_item == -1 ||
      highlight_item == 0)
      highlight_item = getItemCount()-1;
    else
      highlight_item--;
    generateMap();
    break;
  default:
    break;
  }
}

void Menu::joyTrgUp(ui::JoyTrg trg, ui::JoyState /*state*/) {
  switch(trg) {
  case ui::JoyTrg::Btn1:
    selectItem(highlight_item);
    break;
  case ui::JoyTrg::Btn2:
    selectItem(-1);
    break;
  default:
    break;
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
