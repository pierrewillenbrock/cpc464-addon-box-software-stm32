
#pragma once

#include "controls.hpp"
#include "scrollbar.hpp"

namespace ui {
  class ListBox : public Panel {
  public:
    struct Item {
      RefPtr<Icon const> icon;
      std::string text;
    };
  private:
    std::vector<Item> m_items;
    ScrollBar m_scrollbar;
    int m_selected;
    sigc::signal<void,int> m_onSelected;
    sigc::signal<void,int> m_onDblClick;
    enum {Idle, BtnDown1, BtnUp1, BtnDown2 } dblclick_state;
    uint64_t dblclick_start;
    int itemAt(unsigned px, unsigned py);
    void scrollChanged(unsigned int /*position*/);
  public:
    ListBox(Container *parent);
    ~ListBox();
    void addItem(Item const &it);
    void clearItems();
    virtual void redraw();
    virtual void mouseDown(uint8_t button, MouseState mousestate);
    virtual void mouseUp(uint8_t button, MouseState mousestate);
    sigc::signal<void,int> &onSelected() { return m_onSelected; }
    sigc::signal<void,int> &onDblClick() { return m_onDblClick; }
  };
}
