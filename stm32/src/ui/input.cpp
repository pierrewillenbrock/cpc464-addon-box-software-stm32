
#include "input.hpp"
#include <timer.hpp>
#include <fpga/font.h>
#include <sstream>
#include <cmath>

using namespace ui;

Input::Input(Container *parent)
  : SubControl(parent)
  , m_value(0)
  , m_minValue(-0x7ffffff)
  , m_maxValue(0x7ffffff)
  , m_accel(0)
  , m_joyChange(0)
  , m_scroll(0)
  , m_cursor(0)
  , m_flags(0)
  , m_focusMode(NotFocused)
{}

Input::Input()
  : SubControl()
  , m_value(0)
  , m_minValue(-0x7ffffff)
  , m_maxValue(0x7ffffff)
  , m_accel(0)
  , m_joyChange(0)
  , m_scroll(0)
  , m_cursor(0)
  , m_flags(0)
  , m_focusMode(NotFocused)
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
  if(m_parent->isMapped() && m_visible && (m_flags & Numeric))
    m_updownicon = icons.getIcon(Icons::UpDown);
  else
    m_updownicon = NULL;
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
  if(m_parent->isMapped() && visible && (m_flags & Numeric))
    m_updownicon = icons.getIcon(Icons::UpDown);
  else
    m_updownicon = NULL;
}

void Input::redraw(bool no_parent_update) {
  if (!m_visible)
    return;
  assert(m_parent);
  unsigned txt_width = (m_flags & Numeric)?m_width-1:m_width;
  PaletteEntry text, cursor, selection;
  switch(m_focusMode) {
  default:
  case NotFocused:
    text = palette.input_normal;
    cursor = palette.input_cursor;
    selection = palette.input_selection;
    break;
  case Navigate:
    text = palette.input_navigate;
    cursor = palette.input_navigate_cursor;
    selection = palette.input_navigate_selection;
    break;
  case Select:
    text = palette.input_selected;
    cursor = palette.input_selected_cursor;
    selection = palette.input_selected_selection;
    break;
  }
  //left align
  for(unsigned int i = m_scroll;
      i-m_scroll < txt_width && i <= m_text.size(); i++) {
    char c;
    if (i < m_text.size())
      c = m_text[i];
    else
      c = ' ';
    if(m_cursor == i)
      map(i-m_scroll, 0) = font_get_tile(c, cursor.sel, cursor.idx);
    else
      map(i-m_scroll, 0) = font_get_tile(c, text.sel, text.idx);
  }
  for(unsigned int i = m_text.size()+1-m_scroll; i < txt_width; i++)
    map(i, 0) = font_get_tile(' ', text.sel, text.idx);
  if ((m_flags & Numeric) && m_updownicon) {
    if (((m_pressed == Up || m_pressed == Down) && m_mouseOver) ||
      m_focusMode == Navigate)
      map(m_width-1, 0) = m_updownicon->sel_map;
    else
      map(m_width-1, 0) = m_updownicon->def_map;
  }
  if (!no_parent_update)
	  m_parent->updatedMap();
}

void Input::unmapped() {
  m_updownicon = NULL;
}

void Input::mapped() {
  if (m_visible && (m_flags & Numeric))
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
    UI_setFocus(this);
    m_focusMode = Select;
    if (r.x+r.width-8 <= mousestate.x && (m_flags & Numeric)) {
      if (mousestate.y < r.y + 4)
	m_pressed = Up;
      else
	m_pressed = Down;
      m_mouseOver = true;
      m_accel = 0;
      if(!m_pressedTimer) {
        doValueChange();
        m_pressedTimer = Timer_Oneshot(500000, sigc::mem_fun(this, &Input::pressedTimer));
      }
    } else {
      //click in the remaining area: set the cursor position
      m_cursor = (mousestate.x-r.x)/8+m_scroll;
      redraw();
    }
  }
}

void Input::doValueChange()  {
  int nextv = m_value;
  if (m_mouseOver) {
    if(m_pressed == Up) {
      nextv += 1 << (m_accel/8);
      if(nextv > m_maxValue) {
        if(m_flags & WrapAround) {
          nextv = nextv - m_maxValue + m_minValue;
        } else {
          nextv = m_maxValue;
        }
      }
    }
    if(m_pressed == Down) {
      nextv  -= 1 << (m_accel/8);
      if(nextv < m_minValue) {
        if(m_flags & WrapAround) {
          nextv = nextv + m_maxValue - m_minValue;
        } else {
          nextv = m_minValue;
        }
      }
    }
  } else {
    m_accel = 0;
  }
  if(m_joyChange != 0) {
    nextv += m_joyChange;
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
  if (m_pressed != None || m_joyChange != 0)
    m_pressedTimer = Timer_Oneshot(200000, sigc::mem_fun(this, &Input::pressedTimer));
  else
    m_pressedTimer.disconnect();
  doValueChange();
}

void Input::mouseUp(uint8_t button, MouseState mousestate) {
  ui::Rect r = getGlobalRect();
  if (mousestate.x < r.x ||
      mousestate.y < r.y ||
      mousestate.x >= r.x+r.width ||
      mousestate.y >= r.y+r.height) {
    if (button == 0 && mousestate.buttons == 0) {
      m_pressed = None;
      redraw();
    }
    return;
  }
  if (button == 0 && mousestate.buttons == 0) {
    if (m_pressed) {
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

void Input::focusEnter() {
  m_focusMode = Navigate;
  redraw();
}

void Input::focusLeave() {
  m_focusMode = NotFocused;
  m_joyChange = 0;
  redraw();
}

void Input::joyTrgDown(JoyTrg trg, JoyState state) {
  if(m_focusMode == Navigate)
    Control::joyTrgDown(trg, state);
  else if (m_focusMode == Select) {
    switch(trg) {
    case JoyTrg::Left:
      if(m_cursor > 0) {
        m_cursor--;
        redraw();
      }
      break;
    case JoyTrg::Right:
      if(m_cursor < m_text.size()) {
        m_cursor++;
        redraw();
      }
      break;
    default:
      break;
    }
  }
}

void Input::joyTrgUp(JoyTrg trg, JoyState state) {
  switch(trg) {
  case JoyTrg::Btn1:
    switch(m_focusMode) {
    case Navigate:
      m_focusMode = Select;
      redraw();
    case Select:
      //bring up keyboard/numpad
      break;
    default:
      break;
    }
    break;
  case JoyTrg::Btn2:
    if(m_focusMode == Select) {
      m_focusMode = Navigate;
      redraw();
    }
    break;
  default:
    if (m_focusMode == Navigate)
      Control::joyTrgUp(trg, state);
    break;
  }
}

void Input::joyAxis(JoyState state) {
  if (m_focusMode == Select && (m_flags & Numeric)) {
    if(state.y > 64) {
      float ch = powf(2,6-logf(128-state.y)/logf(2));
      m_joyChange = -ch;
      if(!m_pressedTimer) {
        doValueChange();
        m_pressedTimer = Timer_Oneshot(500000, sigc::mem_fun(this, &Input::pressedTimer));
      }
    } else if(state.y < -64) {
      float ch = powf(2,6-logf(128+state.y)/logf(2));
      m_joyChange = ch;
      if (state.y <= -128)
        m_joyChange = 64;
      if(!m_pressedTimer) {
        doValueChange();
        m_pressedTimer = Timer_Oneshot(500000, sigc::mem_fun(this, &Input::pressedTimer));
      }
    } else {
      m_joyChange = 0;
    }
  }
}

// kate: indent-width 2; indent-mode cstyle; replace-tabs on;
