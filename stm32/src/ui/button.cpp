
#include "controls.hpp"
#include <fpga/font.h>

using namespace ui;

Button::Button(Container *parent)
  : SubControl(parent)
{}

Button::Button()
  : SubControl()
  , m_pressed(false)
  , m_focused(false)
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

void Button::redraw(bool no_parent_update) {
  if (!m_visible)
    return;
  assert(m_parent);
  PaletteEntry deco = palette.button_deco, pal = palette.button_normal;
  if (m_focused) {
    deco = palette.button_focus_deco;
    pal = palette.button_focus;
  }
  if(m_pressed) {
    deco = palette.button_pressed_deco;
    pal = palette.button_pressed;
  }
  map(0, 0) = font_get_tile('[', deco.sel, deco.idx);
  map(m_width-1, 0) = font_get_tile(']', deco.sel, deco.idx);
  if ((int)m_text.size()+(m_icon?1:0) <= m_width-2) {
    //center it
    unsigned int ofs = (m_width-2-m_text.size()+(m_icon?1:0))/2;
    if (m_icon) {
      if (m_pressed || m_focused)
	map(ofs,0) = m_icon->def_map;
      else
	map(ofs,0) = m_icon->sel_map;
    }
    for(unsigned int i = 0; i < m_text.size(); i++) {
      map(ofs+i+1,0) = font_get_tile(m_text[i], pal.sel, pal.idx);
    }
    for(unsigned int i = 1; i < ofs-(m_icon?1:0); i++)
      map(i,0) = font_get_tile(' ', deco.sel, deco.idx);
    for(unsigned int i = ofs+m_text.size()+1; (int)i < m_width-1; i++)
      map(i,0) = font_get_tile(' ', deco.sel, deco.idx);
  } else {
    if (m_icon) {
      //abbrev... it
      if (m_pressed || m_focused)
	map(1,0) = m_icon->sel_map;
      else
	map(1,0) = m_icon->def_map;
      for(unsigned int i = 0; (int)i < m_width-2-3-1; i++) {
	map(i+2,0) = font_get_tile(m_text[i], pal.sel, pal.idx);
      }
      for(unsigned int i = 0; i < 3; i++) {
	map(i+m_width-1-3,0) = font_get_tile('.', pal.sel, pal.idx);
      }
    } else {
      //abbrev... it
      for(unsigned int i = 0; (int)i < m_width-2-3; i++) {
	map(i+1,0) = font_get_tile(m_text[i], pal.sel, pal.idx);
      }
      for(unsigned int i = 0; i < 3; i++) {
	map(i+m_width-1-3,0) = font_get_tile('.', pal.sel, pal.idx);
      }
    }
  }
  if(!no_parent_update)
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

void Button::joyTrgDown(ui::JoyTrg trg, ui::JoyState state) {
  if (trg != ui::JoyTrg::Btn1)
    Control::joyTrgDown(trg,state);
}

void Button::joyTrgUp(ui::JoyTrg trg, ui::JoyState state) {
  if (trg == ui::JoyTrg::Btn1)
    m_onClick();
  else
    Control::joyTrgUp(trg,state);
}

void Button::focusEnter() {
  m_focused = true;
  redraw();
}

void Button::focusLeave() {
  m_focused = false;
  redraw();
}

// kate: indent-width 2; indent-mode cstyle;
