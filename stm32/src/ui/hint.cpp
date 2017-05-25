
#include "hint.hpp"
#include <fpga/font.h>
#include <fpga/layout.h>

using namespace ui;

Hint::Hint() {
  m_sprite.setPriority(10);
  m_sprite.setZOrder(50);
  m_sprite.setDoubleSize(false);
}

Hint::~Hint() {
}

void Hint::setPosition(Point p) {
  m_sprite.setPosition(p.x,p.y);
}

void Hint::setText(std::string const &text) {
  m_text = text;
  m_sprite.setSize(m_text.size(), 1);

  if (m_visible) {
    for(unsigned i = 0; i < m_text.size(); i++)
      m_sprite.at(i,0) = font_get_tile(m_text[i], 15, 1);
    m_sprite.updateDone();
  }
}

void Hint::setVisible(bool visible) {
  if (m_visible == visible)
    return;
  m_visible = visible;
  if (visible) {
    for(unsigned i = 0; i < m_text.size(); i++)
      m_sprite.at(i,0) = font_get_tile(m_text[i], 15, 1);
    m_sprite.updateDone();
  }
  m_sprite.setVisible(m_visible);
}

// kate: indent-width 2; indent-mode cstyle; replace-tabs on;
