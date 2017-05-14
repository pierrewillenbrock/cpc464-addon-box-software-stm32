
#pragma once

#include "controls.hpp"

namespace ui {
  class ScrollBar : public SubControl {
  private:
    unsigned m_position;
    unsigned m_size;
    unsigned m_pageSize;
    bool m_vertical;
    unsigned m_nobStart;
    unsigned m_nobSize;
    enum { None, Back, PgBack, Nob, PgFwd, Fwd } m_pressed;
    Point m_pressedMousePos;
    unsigned m_pressedPosition;
    uint32_t m_pressedTimer;
    bool m_mouseOver;
    sigc::signal<void,unsigned> m_onChanged;
    void pressedTimer();
    static void _pressedTimer(void *data);
    void recalcNobPosition();
  public:
    ScrollBar(Container *parent);
    ScrollBar();
    ~ScrollBar();
    void setPosition(unsigned position);
    void setSize(unsigned size);
    void setPageSize(unsigned pageSize);
    void setVertical(bool vertical);
    unsigned position() { return m_position; }
    virtual void redraw();
    virtual void mouseDown(uint8_t button, MouseState mousestate);
    virtual void mouseUp(uint8_t button, MouseState mousestate);
    void mouseMove(int16_t /*dx*/, int16_t /*dy*/, MouseState mousestate);
    sigc::signal<void,unsigned> &onChanged() { return m_onChanged; }
  };
}
