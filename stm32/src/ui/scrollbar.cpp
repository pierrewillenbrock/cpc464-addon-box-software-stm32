
#include "scrollbar.hpp"
#include <fpga/font.h>
#include <timer.hpp>

using namespace ui;

ScrollBar::ScrollBar(Container *parent)
  : SubControl(parent)
  , m_pressed(None)
{}

ScrollBar::ScrollBar()
  : SubControl()
  , m_pressed(None)
{}

ScrollBar::~ScrollBar() {
}

void ScrollBar::recalcNobPosition() {
  unsigned ctl_size = (m_vertical?m_height:m_width);
  if (ctl_size < 2)
    return;
  ctl_size -= 2;

  m_nobStart = (m_position * ctl_size + m_size/2) / m_size;
  m_nobSize = (m_pageSize * ctl_size) / m_size;
  if (m_nobSize == 0)
    m_nobSize = 0;
  if (m_nobSize > ctl_size)
    m_nobSize = ctl_size;
  if (m_nobStart+m_nobSize > ctl_size)
    m_nobStart = ctl_size - m_nobSize;
  if (m_position == 0) {
    if (m_position + m_pageSize == m_size) {
      m_nobStart = 0;
      m_nobSize = ctl_size;
    } else {
      m_nobStart = 0;
      if (m_nobSize == ctl_size)
	m_nobSize = ctl_size - 1;
    }
  } else {
    if (m_position + m_pageSize == m_size) {
      m_nobStart = ctl_size - m_nobSize;
      if (m_nobStart == 0) {
	m_nobStart = 1;
	m_nobSize--;
      }
    } else {
      if (m_nobStart == 0) {
	m_nobStart++;
	if (m_nobStart+m_nobSize == ctl_size)
	  m_nobSize--;
      } else if (m_nobStart+m_nobSize == ctl_size) {
	if(m_nobStart <= 1) {
	  m_nobSize--;
	} else {
	  m_nobStart--;
	}
      }
    }
  }
}

void ScrollBar::setPosition(unsigned position) {
  m_position = position;
  recalcNobPosition();
  redraw();
}

void ScrollBar::setSize(unsigned size) {
  m_size = size;
  recalcNobPosition();
  redraw();
}

void ScrollBar::setPageSize(unsigned pageSize) {
  m_pageSize = pageSize;
  recalcNobPosition();
  redraw();
}

void ScrollBar::setVertical(bool vertical) {
  m_vertical = vertical;
  recalcNobPosition();
  redraw();
}

void ScrollBar::redraw(bool no_parent_update) {
  if (!m_visible)
    return;
  assert(m_parent);

  PaletteEntry back, pgback, nob, pgfwd, fwd;
  back = palette.button_deco;
  pgback = palette.scrollbar_page;
  nob = palette.button_deco;
  pgfwd = palette.scrollbar_page;
  fwd = palette.button_deco;
  switch(m_pressed) {
  case Back:
    back = palette.button_pressed_deco;
    break;
  case PgBack:
    pgback = palette.scrollbar_pressed_page;
    break;
  case Nob:
    nob = palette.button_pressed_deco;
    break;
  case PgFwd:
    pgfwd = palette.scrollbar_pressed_page;
    break;
  case Fwd:
    fwd = palette.button_pressed_deco;
    break;
  default:
    break;
  }

  if (m_vertical) {
    map(0, 0) = font_get_tile('^', back.idx, back.sel);
    for(unsigned i = 0; (int)i < (int)m_height-2; i++) {
      if (i < m_nobStart)
	map(0, i+1) = font_get_tile(' ', pgback.sel, pgback.idx);
      else if (i >= m_nobStart && i < m_nobStart+m_nobSize)
	map(0, i+1) = font_get_tile('#', nob.sel, nob.idx);
      else
	map(0, i+1) = font_get_tile(' ', pgfwd.sel, pgfwd.idx);
    }
    map(0, m_height-1) = font_get_tile('v', fwd.sel, fwd.idx);
  } else {
    map(0, 0) = font_get_tile('<', back.idx, back.sel);
    for(unsigned i = 0; (int)i < (int)m_width-2; i++) {
      if (i < m_nobStart)
	map(i+1, 0) = font_get_tile(' ', pgback.sel, pgback.idx);
      else if (i >= m_nobStart && i < m_nobStart+m_nobSize)
	map(i+1, 0) = font_get_tile('#', nob.sel, nob.idx);
      else
	map(i+1, 0) = font_get_tile(' ', pgfwd.sel, pgfwd.idx);
    }
    map(m_width-1, 0) = font_get_tile('>', fwd.sel, fwd.idx);
  }
  if(!no_parent_update)
    m_parent->updatedMap();
}

void ScrollBar::mouseDown(uint8_t button, MouseState mousestate) {
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
    unsigned pos = (m_vertical?(mousestate.y-r.y):(mousestate.x-r.x))/8;
    unsigned ctl_size = (m_vertical?m_height:m_width);
    if (pos == 0) {
      m_pressed = Back;
      if(m_position > 0) {
	m_position--;
	m_onChanged(m_position);
      }
    } else if (pos == ctl_size-1) {
      m_pressed = Fwd;
      if(m_position+m_pageSize < m_size) {
	m_position++;
	m_onChanged(m_position);
      }
    } else if (pos < m_nobStart) {
      m_pressed = PgBack;
      if(m_position > 0) {
	if (m_position > m_pageSize)
	  m_position -= m_pageSize;
	else
	  m_position = 0;
	m_onChanged(m_position);
      }
    } else if (pos >= m_nobStart + m_nobSize) {
      m_pressed = PgFwd;
      if(m_position+m_pageSize < m_size) {
	if(m_position+2*m_pageSize <= m_size)
	  m_position += m_pageSize;
	else
	  m_position = m_size-m_pageSize;
	m_onChanged(m_position);
      }
    } else {
      m_pressed = Nob;
    }
    if (m_pressed != None && m_pressed != Nob) {
      // install our timer. first shot is 0.5s, then 0.2s
      m_pressedTimer = Timer_Oneshot(500000, sigc::mem_fun(this, &ScrollBar::pressedTimer));
      m_mouseOver = true;
    }
    m_pressedMousePos = Point(mousestate.x,mousestate.y);
    m_pressedPosition = m_position;
  }
  recalcNobPosition();
  redraw();
}

void ScrollBar::pressedTimer() {
  ISR_Guard g;
  m_pressedTimer = Timer_Oneshot(200000, sigc::mem_fun(this, &ScrollBar::pressedTimer));
  if (m_mouseOver) {
    if (m_pressed == Back) {
      if(m_position > 0) {
	m_position--;
	m_onChanged(m_position);
      }
    } else if (m_pressed == Fwd) {
      if(m_position+m_pageSize < m_size) {
	m_position++;
	m_onChanged(m_position);
      }
    } else if (m_pressed == PgBack) {
      if(m_position > 0) {
	if (m_position > m_pageSize)
	  m_position -= m_pageSize;
	else
	  m_position = 0;
	m_onChanged(m_position);
      }
    } else if (m_pressed == PgFwd) {
      if(m_position+m_pageSize < m_size) {
	if(m_position+2*m_pageSize <= m_size)
	  m_position += m_pageSize;
	else
	  m_position = m_size-m_pageSize;
	m_onChanged(m_position);
      }
    }
    recalcNobPosition();
    redraw();
  }
}

void ScrollBar::mouseUp(uint8_t button, MouseState mousestate) {
  ui::Rect r = getGlobalRect();
  if (mousestate.x < r.x ||
      mousestate.y < r.y ||
      mousestate.x >= r.x+r.width ||
      mousestate.y >= r.y+r.height) {
    if (button == 0 && mousestate.buttons == 0) {
      ISR_Guard g;
      m_pressedTimer.disconnect();
      m_pressed = None;
      redraw();
    }
    return;
  }
  if (button == 0 && mousestate.buttons == 0) {
    if (m_pressed) {
      ISR_Guard g;
      m_pressedTimer.disconnect();
      m_pressed = None;
      redraw();
    }
  }
}

void ScrollBar::mouseMove(int16_t /*dx*/, int16_t /*dy*/, MouseState mousestate) {
  if (mousestate.buttons != 1)
    return;
  ui::Rect r = getGlobalRect();
  if (mousestate.x < r.x ||
      mousestate.y < r.y ||
      mousestate.x >= r.x+r.width ||
      mousestate.y >= r.y+r.height)
    m_mouseOver = false;
  else
    m_mouseOver = true;

  if (mousestate.buttons == 1) {
    if (m_pressed == Nob) {
      unsigned ctl_size = (m_vertical?m_height:m_width);
      int delta = m_vertical?(mousestate.y-m_pressedMousePos.y):
	(mousestate.x-m_pressedMousePos.x);
      int pos = (int)m_pressedPosition + delta*(int)m_size/(int)((ctl_size-2)*8);
      if (pos < 0)
	pos = 0;
      if (pos+m_pageSize > m_size)
	pos = m_size-m_pageSize;
      if (m_position != (unsigned)pos) {
	m_position = pos;
	m_onChanged(m_position);
	recalcNobPosition();
	redraw();
      }
    }
  }
  redraw();
}

// kate: indent-width 2; indent-mode cstyle;
