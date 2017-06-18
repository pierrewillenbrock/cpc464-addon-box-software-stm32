
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

void Panel::fullRedraw() {
  m_parent->fullRedraw();
}

void Panel::updatedMap() {
  m_parent->updatedMap();
}

void Panel::redraw(bool no_parent_update) {
  //we don't do any drawing on our own, being well served by the
  //global background. but our children may.
  if (m_visible) {
    for(auto &ch : m_children)
      ch->redraw(true);
  }
  if (!no_parent_update)
	  m_parent->updatedMap();
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

Control *Panel::getNextControl(Direction dir, Control *refctl,
                                  Point &refpt) {
  Control *next = Container::getNextControl(dir, refctl, refpt);
  if (next)
    return next;
  if (m_parent) { // okay, reached the end of the control. ask the parent.
    //we could use SubControl::getNextControl(dir, this, refpt), but that does
    //exactly the same.
    return m_parent->getNextControl(dir, this, refpt);
  }
  return NULL;
}

// kate: indent-width 2; indent-mode cstyle; replace-tabs on;
