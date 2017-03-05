
#pragma once

#include "controls.hpp"

namespace ui {
  class Input : public SubControl {
  private:
    std::string m_text;
    int m_value;
    int m_minValue;
    int m_maxValue;
    int m_accel;
    unsigned m_scroll;
    unsigned m_cursor;
    bool m_focused;
    unsigned m_flags;
    RefPtr<Icon const> m_updownicon;
    enum { None, Up, Down } m_pressed;
    sigc::signal<void,std::string> m_onChanged;
    sigc::signal<void,int> m_onValueChanged;
    uint32_t m_pressedTimer;
    bool m_mouseOver;
    void pressedTimer();
    static void _pressedTimer(void *data);
    void doValueChange();
  public:
    enum Flags {
      Numeric = 1, WrapAround = 2
    };
    Input(Container *parent);
    ~Input();
    void setText(std::string const &text);
    void setFlags(unsigned flags);
    void setValue(int value);
    void setValueBounds(int min, int max);
    virtual void setVisible(bool visible);
    std::string const &text() const { return m_text; }
    int value() const { return m_value; }
    virtual void redraw();
    virtual void mouseDown(uint8_t button, MouseState mousestate);
    virtual void mouseUp(uint8_t button, MouseState mousestate);
    virtual void mouseMove(int16_t /*dx*/, int16_t /*dy*/, MouseState mousestate);
    sigc::signal<void,std::string> &onChanged() { return m_onChanged; }
    sigc::signal<void,int> &onValueChanged() { return m_onValueChanged; }
    virtual void unmapped();
    virtual void mapped();
  };

}
