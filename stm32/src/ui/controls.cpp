
#include "controls.hpp"

using namespace ui;

Control *Container::getChildAt(Point p) {
  //p is local coordinates. keep it like that.
  for(auto &ch : m_children) {
    Rect r = ch->getRect();
    if (r.x <= p.x && r.y <= p.y &&
	r.x+r.width > p.x && r.y+r.height > p.y) {
      Control *ctl = ch->getChildAt(Point(p.x-r.x,p.y-r.y));
      if (ctl)
	return ctl;
      return ch;
    }
  }
  return NULL;
}

SubControl::SubControl(Container *parent)
  : m_parent(parent)
{
}

Rect SubControl::getRect() {
  Rect r = {
    (uint16_t)(m_x*8), (uint16_t)(m_y*8),
    (uint16_t)(m_width*8), (uint16_t)(m_height*8)
  };
  return r;
}

Rect SubControl::getGlobalRect() {
  Rect r1 = m_parent->getGlobalRect();
  Rect r2 = getRect();
  r2.x += r1.x;
  r2.y += r1.y;
  return r2;
}

void SubControl::setPosition(unsigned x, unsigned y) {
  m_x = x;
  m_y = y;
  m_parent->fullRedraw();
}

void SubControl::setSize(unsigned width, unsigned height) {
  m_width = width;
  m_height = height;
  m_parent->fullRedraw();
}

void SubControl::setVisible(bool visible) {
  m_visible = visible;
  m_parent->fullRedraw();
}


