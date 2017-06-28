
#include "controls.hpp"

using namespace ui;

ui::Palette ui::palette = {
  .window = {15, 1},
  .hint = {15, 1},
  .menu = {15, 1},
  .menu_selected = {11, 1},
  .notify = {15, 1},
  .input_normal = {11, 1},
  .input_cursor = {11, 1},
  .input_selection = {11, 1},
  .input_navigate = {15, 1},
  .input_navigate_cursor = {15, 1},
  .input_navigate_selection = {15, 1},
  .input_selected = {11, 1},
  .input_selected_cursor = {15, 1},
  .input_selected_selection = {11, 1},
  .listbox_background = {11, 1},
  .listbox_item = {11, 1},
  .listbox_selection = {15, 1},
  .listbox_navigate_background = {15, 1},
  .listbox_navigate_item = {15, 1},
  .listbox_navigate_selection = {15, 1},
  .listbox_selected_background = {11, 1},
  .listbox_selected_item = {11, 1},
  .listbox_selected_selection = {15, 1},
  .button_normal = {15, 1},
  .button_focus = {11, 1},
  .button_pressed = {11, 1},
  .button_deco = {15, 1},
  .button_focus_deco = {11, 1},
  .button_pressed_deco = {11, 1},
  .scrollbar_page = {15, 1},
  .scrollbar_pressed_page = {11, 1},
};


void Container::addChild(SubControl *ctl) {
  m_children.push_back(ctl);
  if (ctl->tabOrder() == ~0U)
    ctl->setTabOrder(m_children.size()*2);
  fullRedraw();
}

void Container::removeChild(SubControl *ctl) {
  for(auto it = m_children.begin(); it != m_children.end(); it++) {
    if(*it == ctl) {
      m_children.erase(it);
      break;
    }
  }
  fullRedraw();
}

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

Control *Container::getNextControl(Direction dir, Control *refctl,
                                    Point &refpt) {
  //this looks through its children to find one in the given direction.
  //Tab order is just LTR, for now, but needs something more explicit,
  //like a tab order field.
  SubControl *next = NULL;
  Rect next_r = {0,0,0,0};
  //if refctl is not a SubControl, it is outside of us anyway, thus we need
  //to behave the same as if it is NULL.
  SubControl *ref = dynamic_cast<SubControl*>(refctl);
  for(auto &ch : m_children) {
    if(ch == ref)
      continue;
    if(!ch->focusable())
      continue;
    Rect r = ch->getGlobalRect();
    switch(dir) {
    case Direction::Up:
      if(r.x <= refpt.x && r.x + r.width > refpt.x &&
          r.y < refpt.y && (!next || r.y > next_r.y)) {
        next = ch;
        next_r = r;
      }
      break;
    case Direction::Down:
      if(r.x <= refpt.x && r.x + r.width > refpt.x &&
          r.y > refpt.y && (!next || r.y < next_r.y)) {
        next = ch;
        next_r = r;
      }
      break;
    case Direction::Right:
      if(r.y <= refpt.y && r.y + r.height > refpt.y &&
          r.x > refpt.x && (!next || r.x < next_r.x)) {
        next = ch;
        next_r = r;
      }
      break;
    case Direction::Left:
      if(r.y <= refpt.y && r.y + r.height > refpt.y &&
          r.x < refpt.x && (!next || r.x > next_r.x)) {
        next = ch;
        next_r = r;
      }
      break;
    case Direction::Tab:
      if((!ref || (ref && ch->tabOrder() > ref->tabOrder())) &&
          (!next || (next && ch->tabOrder() < next->tabOrder()))) {
        next = ch;
        next_r = r;
      }
      break;
    case Direction::Backtab:
      if((!ref || (ref && ch->tabOrder() < ref->tabOrder())) &&
          (!next || (next && ch->tabOrder() > next->tabOrder()))) {
        next = ch;
        next_r = r;
      }
      break;
    }
  }
  if (next)
    return next->getNextControl(dir,NULL,refpt);
  //try harder, now using the rectangle of the reference control
  if (ref) {
    Rect ref_r = ref->getGlobalRect();
    for(auto &ch : m_children) {
      if(ch == ref)
        continue;
      if(!ch->focusable())
        continue;
      Rect r = ch->getGlobalRect();
      switch(dir) {
      case Direction::Up:
        if(r.x < ref_r.x+ref_r.width && r.x + r.width > ref_r.x &&
            r.y < refpt.y && (!next || r.y > next_r.y)) {
          next = ch;
          next_r = r;
        }
        break;
      case Direction::Down:
        if(r.x < ref_r.x+ref_r.width && r.x + r.width > ref_r.x &&
            r.y > refpt.y && (!next || r.y < next_r.y)) {
          next = ch;
          next_r = r;
        }
        break;
      case Direction::Right:
        if(r.y < ref_r.y + ref_r.height && r.y + r.height > ref_r.y &&
            r.x > refpt.x && (!next || r.x < next_r.x)) {
          next = ch;
          next_r = r;
        }
        break;
      case Direction::Left:
        if(r.y <= refpt.y && r.y + r.height > refpt.y &&
            r.x < refpt.x && (!next || r.x > next_r.x)) {
          next = ch;
          next_r = r;
        }
        break;
      default:
	break;
      }
    }
  }
  if (next)
    return next->getNextControl(dir,NULL,refpt);
  return NULL;
}

SubControl::SubControl(Container *parent)
  : m_x(0)
  , m_y(0)
  , m_width(0)
  , m_height(0)
  , m_visible(false)
  , m_parent(parent)
  , m_taborder(~0U)
{
}

SubControl::SubControl()
  : m_x(0)
  , m_y(0)
  , m_width(0)
  , m_height(0)
  , m_visible(false)
  , m_parent(nullptr)
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

void SubControl::setParent(Container *parent) {
  m_parent = parent;
}

Control *SubControl::getNextControl(Direction dir, Control *refctl,
                                    Point &refpt) {
  if (!refctl) {
    //we have been selected, we are a single entity subcontrol, so just clamp
    //the refpt to our bounds.
    Rect r = getGlobalRect();
    if (refpt.x < r.x)
      refpt.x = r.x;
    if (refpt.x >= r.x + r.width)
      refpt.x = r.x + r.width-1;
    if (refpt.y < r.y)
      refpt.y = r.y;
    if (refpt.y >= r.y + r.height)
      refpt.y = r.y + r.height-1;
    return this;
  }
  if (m_parent)
    return m_parent->getNextControl(dir, this, refpt);
  return NULL;
}

// kate: indent-width 2; indent-mode cstyle;
