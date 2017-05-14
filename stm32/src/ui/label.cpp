
#include "controls.hpp"
#include <fpga/font.h>

using namespace ui;

Label::Label(Container *parent)
  : SubControl(parent)
{}

Label::Label()
  : SubControl()
{}

Label::~Label() {
}

void Label::setText(std::string const &text) {
  m_text = text;
  redraw();
}

void Label::redraw() {
  if (!m_visible)
    return;
  assert(m_parent);
  uint32_t *map = m_parent->map();
  unsigned mappitch = m_parent->mapPitch();
  map += m_x + mappitch * m_y;

  if ((int)m_text.size() <= m_width) {
    for(unsigned int i = 0; i < m_text.size(); i++) {
      map[i] = font_get_tile(m_text[i], 15, 1);
    }
  } else {
    //abbrev... it
    for(unsigned int i = 0; (int)i < m_width-3; i++) {
      map[i] = font_get_tile(m_text[i], 15, 1);
    }
    for(unsigned int i = 0; i < 3; i++) {
      map[i+m_width-3] = font_get_tile('.', 15, 1);
    }
  }
  m_parent->updatedMap();
}

