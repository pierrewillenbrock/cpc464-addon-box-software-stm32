
#include "controls.hpp"
#include <fpga/font.h>

using namespace ui;

Button::Button(Container *parent)
  : SubControl(parent)
{}

Button::~Button() {
}

void Button::setText(std::string const &text) {
  m_text = text;
  redraw();
}

void Button::setIcon(RefPtr<Icon const> const &icon) {
  m_icon = icon;
  redraw();
}

void Button::redraw() {
  if (!m_visible)
    return;
  uint32_t *map = m_parent->map();
  unsigned mappitch = m_parent->mapPitch();
  map += m_x + mappitch * m_y;
  uint8_t pal = m_pressed?11:15;
  map[0] = font_get_tile('[',pal, 1);
  map[m_width-1] = font_get_tile(']',pal, 1);
  if ((int)m_text.size()+(m_icon?1:0) <= m_width-2) {
    //center it
    unsigned int ofs = (m_width-2-m_text.size()+(m_icon?1:0))/2;
    if (m_icon) {
      if (m_pressed)
	map[ofs] = m_icon->def_map;
      else
	map[ofs] = m_icon->sel_map;
    }
    for(unsigned int i = 0; i < m_text.size(); i++) {
      map[ofs+i+1] = font_get_tile(m_text[i], pal, 1);
    }
    for(unsigned int i = 1; i < ofs-(m_icon?1:0); i++)
      map[i] = font_get_tile(' ', pal, 1);
    for(unsigned int i = ofs+m_text.size()+1; (int)i < m_width-1; i++)
      map[i] = font_get_tile(' ', pal, 1);
  } else {
    if (m_icon) {
      //abbrev... it
      if (m_pressed)
	map[1] = m_icon->sel_map;
      else
	map[1] = m_icon->def_map;
      for(unsigned int i = 0; (int)i < m_width-2-3-1; i++) {
	map[i+2] = font_get_tile(m_text[i], pal, 1);
      }
      for(unsigned int i = 0; i < 3; i++) {
	map[i+m_width-1-3] = font_get_tile('.', pal, 1);
      }
    } else {
      //abbrev... it
      for(unsigned int i = 0; (int)i < m_width-2-3; i++) {
	map[i+1] = font_get_tile(m_text[i], pal, 1);
      }
      for(unsigned int i = 0; i < 3; i++) {
	map[i+m_width-1-3] = font_get_tile('.', pal, 1);
      }
    }
  }
  m_parent->updatedMap();
}

void Button::mouseDown(uint8_t button, MouseState mousestate) {
  ui::Rect r = getGlobalRect();
  if (mousestate.x < r.x ||
      mousestate.y < r.y ||
      mousestate.x >= r.x+r.width ||
      mousestate.y >= r.y+r.height) {
    if (button == 0 && mousestate.buttons == 1)
      m_pressed = false;
    return;
  }
  if (button == 0 && mousestate.buttons == 1)
    m_pressed = true;
  redraw();
}

void Button::mouseUp(uint8_t button, MouseState mousestate) {
  ui::Rect r = getGlobalRect();
  if (mousestate.x < r.x ||
      mousestate.y < r.y ||
      mousestate.x >= r.x+r.width ||
      mousestate.y >= r.y+r.height) {
    if (button == 0 && mousestate.buttons == 0) {
      m_pressed = false;
      redraw();
    }
    return;
  }
  if (button == 0 && mousestate.buttons == 0) {
    if (m_pressed) {
      m_pressed = false;
      redraw();
      m_onClick();
    }
  }
}
