
#include "input.hpp"
#include <timer.h>
#include <fpga/font.h>
#include <sstream>

using namespace ui;

Input::Input(Container *parent)
  : SubControl(parent)
  , m_value(0)
  , m_minValue(-0x7ffffff)
  , m_maxValue(0x7ffffff)
  , m_accel(0)
  , m_scroll(0)
  , m_cursor(0)
  , m_focused(false)
  , m_flags(0)
{}

Input::Input()
  : SubControl()
  , m_value(0)
  , m_minValue(-0x7ffffff)
  , m_maxValue(0x7ffffff)
  , m_accel(0)
  , m_scroll(0)
  , m_cursor(0)
  , m_focused(false)
  , m_flags(0)
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

void Input::setFlags(unsigned flags) {
  m_flags = flags;
  redraw();
}

void Input::setValue(int value) {
  m_value = value;
  std::stringstream ss;
  ss << value;
  setText(ss.str());
}

void Input::setValueBounds(int min, int max) {
  m_minValue = min;
  m_maxValue = max;
}

void Input::setVisible(bool visible) {
  SubControl::setVisible(visible);
  assert(m_parent);
  if (m_parent->isMapped()) {
    if (visible)
      m_updownicon = icons.getIcon(Icons::UpDown);
    else
      m_updownicon = NULL;
  }
}

void Input::redraw() {
  if (!m_visible)
    return;
  assert(m_parent);
  uint32_t *map = m_parent->map();
  unsigned mappitch = m_parent->mapPitch();
  map += m_x + mappitch * m_y;
  unsigned txt_width = (m_flags & Numeric)?m_width-1:m_width;
  //left align
  for(unsigned int i = m_scroll;
      i-m_scroll < txt_width && i <= m_text.size(); i++) {
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
  for(unsigned int i = m_text.size()+1-m_scroll; i < txt_width; i++)
    map[i] = font_get_tile(' ', 11, 1);
  if ((m_flags & Numeric) && m_updownicon) {
    if ((m_pressed == Up || m_pressed == Down) && m_mouseOver)
      map[m_width-1] = m_updownicon->sel_map;
    else
      map[m_width-1] = m_updownicon->def_map;
  }
  m_parent->updatedMap();
}

void Input::unmapped() {
  if (m_visible)
    m_updownicon = NULL;
}

void Input::mapped() {
  if (m_visible)
    m_updownicon = icons.getIcon(Icons::UpDown);
}

void Input::mouseDown(uint8_t button, MouseState mousestate) {
  ui::Rect r = getGlobalRect();
  if (mousestate.x < r.x ||
      mousestate.y < r.y ||
      mousestate.x >= r.x+r.width ||
      mousestate.y >= r.y+r.height) {
    if (button == 0 && mousestate.buttons == 1)
      m_pressed = None;
    return;
  }

  if (button == 0 && mousestate.buttons == 1) {
    if (r.x <= 8 + mousestate.x && (m_flags & Numeric)) {
      //todo: move key input focus to this control
      if (mousestate.y < r.y + 4)
	m_pressed = Up;
      else
	m_pressed = Down;
      m_mouseOver = true;
      m_accel = 0;
      doValueChange();
      m_pressedTimer = Timer_Oneshot(500000, _pressedTimer, this);
    } else {
      //click in the remaining area: move input focus here and set the cursor
      //position. todo
      redraw();
    }
  }
}

void Input::doValueChange()  {
  int nextv = m_value;
  if (m_pressed == Up) {
    nextv += 1 << (m_accel/8);
    if (nextv > m_maxValue) {
      if (m_flags & WrapAround) {
	nextv = nextv - m_maxValue + m_minValue;
      } else {
	nextv = m_maxValue;
      }
    }
  }
  if (m_pressed == Down) {
    nextv  -= 1 << (m_accel/8);
    if (nextv < m_minValue) {
      if (m_flags & WrapAround) {
	nextv = nextv + m_maxValue - m_minValue;
      } else {
	nextv = m_minValue;
      }
    }
  }
  if (1 << (m_accel/8) < (m_maxValue - m_minValue) / 8)
    m_accel++;
  if (m_value != nextv) {
    m_value = nextv;
    m_onValueChanged(m_value);
    setValue(m_value);
    redraw();
  }
}

void Input::pressedTimer() {
  ISR_Guard g;
  m_pressedTimer = Timer_Oneshot(200000, _pressedTimer, this);
  if (m_mouseOver) {
    doValueChange();
  } else {
    m_accel = 0;
  }
}

void Input::_pressedTimer(void *data) {
  Input *_this = (Input *)data;
  _this->pressedTimer();
}

void Input::mouseUp(uint8_t button, MouseState mousestate) {
  ui::Rect r = getGlobalRect();
  if (mousestate.x < r.x ||
      mousestate.y < r.y ||
      mousestate.x >= r.x+r.width ||
      mousestate.y >= r.y+r.height) {
    if (button == 0 && mousestate.buttons == 0) {
      ISR_Guard g;
      Timer_Cancel(m_pressedTimer);
      m_pressed = None;
      redraw();
    }
    return;
  }
  if (button == 0 && mousestate.buttons == 0) {
    if (m_pressed) {
      ISR_Guard g;
      Timer_Cancel(m_pressedTimer);
      m_pressed = None;
      redraw();
    }
  }
}

void Input::mouseMove(int16_t /*dx*/, int16_t /*dy*/, MouseState mousestate) {
  if (mousestate.buttons != 1)
    return;
  ui::Rect r = getGlobalRect();
  if (mousestate.x < r.x ||
      mousestate.y < r.y ||
      mousestate.x >= r.x+r.width ||
      mousestate.y >= r.y+r.height) {
    if (m_mouseOver) {
      m_mouseOver = false;
      redraw();
    }
  } else {
    if (!m_mouseOver) {
      m_mouseOver = true;
      redraw();
    }
  }
}

