
#include "listbox.hpp"
#include <fpga/font.h>
#include <timer.hpp>

using namespace ui;

ListBox::ListBox(Container *parent)
  : Panel(parent)
  , m_scrollbar(this)
  , m_selected(-1)
  , m_focusMode(NotFocused)
{
  addChild(&m_scrollbar);
  m_scrollbar.onChanged().connect(sigc::mem_fun(this, &ListBox::scrollChanged));
}

ListBox::ListBox()
  : Panel()
  , m_scrollbar(this)
  , m_selected(-1)
  , m_focusMode(NotFocused)
{
  addChild(&m_scrollbar);
  m_scrollbar.onChanged().connect(sigc::mem_fun(this, &ListBox::scrollChanged));
}

ListBox::~ListBox() {
}

void ListBox::addItem(Item const &it) {
  m_items.push_back(it);

  unsigned rows = m_height;
  unsigned x = 0;
  for(unsigned c = 0; c < (m_items.size()+rows-1)/rows; c++) {
    unsigned w = 0;
    for(unsigned r = 0; r < rows && r+c*rows < m_items.size(); r++) {
      Item &it = m_items[r+c*rows];
      if (w < it.text.size())
	w = it.text.size();
    }
    w++;
    x += w;
  }
  if (x > m_width) {

    rows--;
    unsigned x = 0;
    for(unsigned c = 0; c < (m_items.size()+rows-1)/rows; c++) {
      unsigned w = 0;
      for(unsigned r = 0; r < rows && r+c*rows < m_items.size(); r++) {
	Item &it = m_items[r+c*rows];
	if (w < it.text.size())
	  w = it.text.size();
      }
      w++;
      x += w;
    }

    m_scrollbar.setVisible(true);
    m_scrollbar.setPageSize(m_width);
    m_scrollbar.setSize(x);
    static_cast<SubControl*>(&m_scrollbar)->setPosition(0,m_height-1);
    static_cast<SubControl*>(&m_scrollbar)->setSize(m_width,1);
  } else {
    m_scrollbar.setVisible(false);
  }

  redraw();
}

void ListBox::clearItems() {
  m_items.clear();
  m_scrollbar.setVisible(false);
  redraw();
}

void ListBox::redraw(bool no_parent_update) {
  if (!m_visible)
    return;
  assert(m_parent);
  unsigned rows = m_scrollbar.visible()?m_height-1:m_height;
  unsigned position = m_scrollbar.visible()?m_scrollbar.position():0;

  unsigned bgpal = (m_focusMode == Navigate)?15:11;
  int x = -position;
  for(unsigned c = 0; c < (m_items.size()+rows-1)/rows; c++) {
    unsigned w = 0;
    for(unsigned r = 0; r < rows && r+c*rows < m_items.size(); r++) {
      Item &it = m_items[r+c*rows];
      if (w < it.text.size())
	w = it.text.size();
    }
    w++;
    for(unsigned r = 0; r < rows && r+c*rows < m_items.size(); r++) {
      Item &it = m_items[r+c*rows];
      bool highlight = ((int)(r+c*rows) == m_selected) ||
                       m_focusMode == Navigate;
      unsigned pal = highlight?15:11;
      if (x >= 0) {
	if (it.icon)
	  map(x,r) = highlight?it.icon->sel_map:it.icon->def_map;
	else
	  map(x,r) = font_get_tile(' ',bgpal, 1);
      }
      if (x+1 + it.text.size() > 0) {
	for(int i = 0; i < (int)it.text.size(); i++) {
	  if (x+i+1 >= 0 && x+i+1 < m_width)
	    map(x+i+1, r) = font_get_tile(it.text[i],pal, 1);
	}
	for(int i = it.text.size()+1; i < (int)w; i++) {
	  if (x+i >= 0 && x+i < m_width)
	    map(x+i, r) = font_get_tile(' ',bgpal, 1);
	}
      }
    }
    if (rows > m_items.size()-c*rows) {
      for(unsigned r = m_items.size()-c*rows; r < rows; r++) {
	for(int i = 0; i < (int)w; i++) {
	  if (x+i >= 0 && x+i < m_width)
	    map(x+i, r) = font_get_tile(' ',bgpal, 1);
	}
      }
    }
    x += w;
    if (x >= m_width)
      break;
  }
  if (x < m_width) {
    for(unsigned r = 0; r < rows; r++) {
      for(unsigned int i = x; i < m_width; i++) {
	map(i, r) = font_get_tile(' ',bgpal, 1);
      }
    }
  }
  Panel::redraw(no_parent_update);
  if (!no_parent_update)
    m_parent->updatedMap();
}

void ListBox::makeItemVisible(int item) {
  if (!m_scrollbar.visible())
    return;
  unsigned rows = m_height-1;
  unsigned position = m_scrollbar.position();

  unsigned x = 0;
  for(unsigned c = 0; c < (m_items.size()+rows-1)/rows; c++) {
    unsigned w = 0;
    for(unsigned r = 0; r < rows && r+c*rows < m_items.size(); r++) {
      Item &it = m_items[r+c*rows];
      if (w < it.text.size())
	w = it.text.size();
    }
    w++;
    for(unsigned r = 0; r < rows && r+c*rows < m_items.size(); r++) {
      Item &it = m_items[r+c*rows];
      if (r+c*rows == (unsigned)item) {
	if (x < position) {
	  m_scrollbar.setPosition(x);
	} else if (x+it.text.size() > position + m_width) {
	  int np = x + it.text.size() - m_width;
	  if (np < 0)
	    np = 0;
	  if ((int)x < np)
	    np = x;
	  if ((unsigned)np != position)
            m_scrollbar.setPosition(np);
	}
	return;
      }
    }
    x += w;
  }
}

int ListBox::itemAt(unsigned px, unsigned py) {
  unsigned rows = m_scrollbar.visible()?m_height-1:m_height;
  unsigned position = m_scrollbar.visible()?m_scrollbar.position():0;

  int x = -position;
  for(unsigned c = 0; c < (m_items.size()+rows-1)/rows; c++) {
    unsigned w = 0;
    for(unsigned r = 0; r < rows && r+c*rows < m_items.size(); r++) {
      Item &it = m_items[r+c*rows];
      if (w < it.text.size())
	w = it.text.size();
    }
    w++;
    for(unsigned r = 0; r < rows && r+c*rows < m_items.size(); r++) {
      Item &it = m_items[r+c*rows];
      if ((int)px > x && (int)px < x + 1 + (int)it.text.size() && py == r)
	return r+c*rows;
    }
    x += w;
    if (x >= m_width)
      break;
  }
  return -1;
}

void ListBox::scrollChanged(unsigned /*position*/) {
  redraw();
}

void ListBox::mouseDown(uint8_t button, MouseState mousestate) {
  //find the selected item
  Rect r = getGlobalRect();
  if (button == 0 && mousestate.buttons == 1) {
    int item = itemAt((mousestate.x - r.x)/8, (mousestate.y - r.y)/8);
    if (m_selected != item) {
      dblclick_state = Idle;
      m_selected = item;
      m_onSelected(item);
      redraw();
    }
    if (dblclick_state == Idle) {
      dblclick_start = Timer_timeSincePowerOn();
      dblclick_state = BtnDown1;
    }
    if (dblclick_state == BtnUp1) {
      uint64_t t = Timer_timeSincePowerOn();
      if (t - dblclick_start < 500000)
	dblclick_state = BtnDown2;
      else
	dblclick_state = BtnDown1;
    }
  }
}

void ListBox::mouseUp(uint8_t button, MouseState mousestate) {
  //todo: double click detection
  Rect r = getGlobalRect();
  int item = itemAt((mousestate.x - r.x)/8, (mousestate.y - r.y)/8);
  if (button == 0 && mousestate.buttons == 0) {
    if (m_selected != item) {
      dblclick_state = Idle;
    }
    if (dblclick_state == BtnDown1) {
      dblclick_start = Timer_timeSincePowerOn();
      uint64_t t = Timer_timeSincePowerOn();
      if (t - dblclick_start < 500000)
	dblclick_state = BtnUp1;
      else
	dblclick_state = Idle;
    }
    if (dblclick_state == BtnDown2) {
      uint64_t t = Timer_timeSincePowerOn();
      if (t - dblclick_start < 500000) {
	m_onDblClick(m_selected);
      }
      dblclick_state = Idle;
    }
  }
}

void ListBox::joyTrgDown(JoyTrg trg, JoyState state) {
  if (m_focusMode == Navigate)
    Control::joyTrgDown(trg, state);
  else {
    unsigned rows = m_scrollbar.visible()?m_height-1:m_height;
    int item = m_selected;
    switch(trg) {
    case JoyTrg::Left:
      if(item == -1)
        item = m_items.size()-1;
      else if(item >= (int)rows)
	item -= rows;
      break;
    case JoyTrg::Right:
      if (item == -1)
	item = 0;
      else if (item < (int)m_items.size() - (int)rows)
	item += rows;
      break;
    case JoyTrg::Up:
    case JoyTrg::Previous:
      if (item == -1)
        item = m_items.size()-1;
      else if(item > 0 )
        item--;
      break;
    case JoyTrg::Down:
    case JoyTrg::Next:
      if (item == -1)
        item = 0;
      else if(item < (int)m_items.size()-1)
        item++;
      break;
    default:
      break;
    }
    if (item != m_selected) {
      m_selected = item;
      makeItemVisible(m_selected);
      m_onSelected(m_selected);
      redraw();
    }
  }
}

void ListBox::joyTrgUp(JoyTrg trg, JoyState state) {
  switch(trg) {
  case JoyTrg::Btn1:
    if (m_selected != -1)
      m_onDblClick(m_selected);
    break;
  //select current item
  case JoyTrg::Btn2:
    if(m_focusMode == Navigate)
      m_focusMode = Select;
    else if (m_focusMode == Select)
      m_focusMode = Navigate;
    redraw();
    break;
  default:
    Control::joyTrgUp(trg, state);
    break;
  }
}

void ListBox::focusEnter() {
  m_focusMode = Navigate;
  redraw();
}

void ListBox::focusLeave() {
  m_focusMode = NotFocused;
  redraw();
}

Control *ListBox::getNextControl(Direction dir, Control *refctl,
                                  Point &refpt) {
  return SubControl::getNextControl(dir, refctl, refpt);
}

// kate: indent-width 2; indent-mode cstyle;
