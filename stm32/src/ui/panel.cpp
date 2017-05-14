
#include "controls.hpp"

using namespace ui;

Panel::Panel(Container *parent)
  : SubControl(parent)
{
}

Panel::Panel()
  : SubControl()
{
}

Panel::~Panel() {
}

uint32_t *Panel::map() {
  return m_parent->map()+mapPitch()*m_y+m_x;
}

unsigned Panel::mapPitch() {
  return m_parent->mapPitch();
}

void Panel::fullRedraw() {
  m_parent->fullRedraw();
}

void Panel::updatedMap() {
  m_parent->updatedMap();
}

void Panel::redraw() {
  //we don't do any drawing on our own, being well served by the
  //global background. but our children may.
  if (m_visible) {
    for(auto &ch : m_children)
      ch->redraw();
  }
}

void Panel::unmapped() {
  SubControl::unmapped();
  if (m_visible) {
    for(auto &ch : m_children)
      ch->unmapped();
  }
}

void Panel::mapped() {
  SubControl::mapped();
  if (m_visible) {
    for(auto &ch : m_children)
      ch->mapped();
  }
}

void Panel::setVisible(bool visible) {
  if (m_visible == visible)
    return;
  SubControl::setVisible(visible);
  if (m_parent->isMapped()) {
    if (visible) {
      for(auto &ch : m_children)
	ch->mapped();
    } else {
      for(auto &ch : m_children)
	ch->unmapped();
    }
  }
}
