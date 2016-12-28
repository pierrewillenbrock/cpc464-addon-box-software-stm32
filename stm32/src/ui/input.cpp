
#include "controls.hpp"
#include <fpga/font.h>

using namespace ui;

Input::Input(Container *parent)
  : SubControl(parent)
  , m_scroll(0)
  , m_cursor(0)
  , m_focused(false)
{}

Input::~Input() {
}

void Input::setText(std::string const &text) {
  m_text = text;
  if (m_text.size() < m_width)
    m_scroll = 0;
  else if (m_text.size()+1-m_scroll < m_width)
    m_scroll = m_text.size()+1-m_width;
  if (m_cursor > m_text.size())
    m_cursor = m_text.size();
  redraw();
}

void Input::redraw() {
  if (!m_visible)
    return;
  uint32_t *map = m_parent->map();
  unsigned mappitch = m_parent->mapPitch();
  map += m_x + mappitch * m_y;
  //left align
  for(unsigned int i = m_scroll;
      i-m_scroll < m_width && i <= m_text.size(); i++) {
    char c;
    if (i < m_text.size())
      c = m_text[i];
    else
      c = ' ';
    if (m_focused && m_cursor == i)
      map[i-m_scroll] = font_get_tile(c, 15, 1);
    else
      map[i-m_scroll] = font_get_tile(c, 11, 1);
  }
  for(unsigned int i = m_text.size()+1-m_scroll; (int)i < m_width; i++)
    map[i] = font_get_tile(' ', 11, 1);
  m_parent->updatedMap();
}

