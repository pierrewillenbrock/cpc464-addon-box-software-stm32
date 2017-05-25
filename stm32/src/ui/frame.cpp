
#include "frame.hpp"
#include <fpga/font.h>
#include <fpga/layout.h>

using namespace ui;

Frame::Frame() {
  m_sprite.setPriority(20);
  m_sprite.setZOrder(30);
  m_sprite.setSize(0,0);
  m_sprite.setDoubleSize(false);
}

Frame::~Frame() {
}

Rect Frame::getRect() {
  Rect r = {
    m_position.x, m_position.y, (uint16_t)(m_width*8), (uint16_t)(m_height *8)
  };
  return r;
}

Rect Frame::getGlobalRect() {
  return getRect();
}

void Frame::fullRedraw() {
  if (!m_visible)
    return;
  uint32_t empty_tile = font_get_tile(' ',15, 1);
  for(unsigned y = 0; y < m_height; y++)
    for(unsigned x = 0; x < m_width; x++)
      map(x,y) = empty_tile;
  for(auto &ch : m_children)
    ch->redraw(true);
  m_sprite.updateDone();
}

void Frame::updatedMap() {
  m_sprite.updateDone();
}

void Frame::setPosition(Point p) {
  m_position = p;
  m_sprite.setPosition(p.x,p.y);
}

void Frame::setSize(unsigned width, unsigned height) {
  m_sprite.setSize(width, height);
  m_width = width;
  m_height = height;
}

void Frame::setVisible(bool visible) {
  if (m_visible == visible)
    return;
  m_visible = visible;
  m_sprite.setVisible(m_visible);
  if (m_visible) {
    for(auto &ch : m_children)
      ch->mapped();
    fullRedraw();
  } else {
    for(auto &ch : m_children)
      ch->unmapped();
  }
}

// kate: indent-width 2; indent-mode cstyle;
