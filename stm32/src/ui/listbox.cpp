
#include "listbox.hpp"
#include <fpga/font.h>
#include <timer.h>

using namespace ui;

ListBox::ListBox(Container *parent)
  : Panel(parent)
  , m_scrollbar(this)
  , m_selected(-1)
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

void ListBox::redraw() {
  if (!m_visible)
    return;
  uint32_t *map = m_parent->map();
  unsigned mappitch = m_parent->mapPitch();
  map += m_x + mappitch * m_y;
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
      bool selected = ((int)(r+c*rows) == m_selected);
      unsigned pal = selected?15:11;
      if (x >= 0) {
	if (it.icon)
	  map[r * mappitch + x] = selected?it.icon->sel_map:it.icon->def_map;
	else
	  map[r * mappitch + x] = font_get_tile(' ',11, 1);
      }
      if (x+1 + it.text.size() > 0) {
	for(int i = 0; i < (int)it.text.size(); i++) {
	  if (x+i+1 >= 0 && x+i+1 < m_width)
	    map[r * mappitch + x+i+1] = font_get_tile(it.text[i],pal, 1);
	}
	for(int i = it.text.size()+1; i < (int)w; i++) {
	  if (x+i >= 0 && x+i < m_width)
	    map[r * mappitch + x+i] = font_get_tile(' ',11, 1);
	}
      }
    }
    if (rows > m_items.size()-c*rows) {
      for(unsigned r = m_items.size()-c*rows; r < rows; r++) {
	for(int i = 0; i < (int)w; i++) {
	  if (x+i >= 0 && x+i < m_width)
	    map[r * mappitch + x+i] = font_get_tile(' ',11, 1);
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
	map[r * mappitch + i] = font_get_tile(' ',11, 1);
      }
    }
  }
  Panel::redraw();
  m_parent->updatedMap();
}

int ListBox::itemAt(unsigned px, unsigned py) {
  unsigned rows = m_scrollbar.visible()?m_height-1:m_height;
  unsigned position = m_scrollbar.visible()?m_scrollbar.position():0;

  unsigned x = -position;
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
      if (px > x && px < x + 1 + it.text.size() && py == r)
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

