
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
    enum FocusMode {
      NotFocused, Navigate, Select
    };
    FocusMode m_focusMode;
    sigc::signal<void,int> m_onSelected;
    sigc::signal<void,int> m_onDblClick;
    enum {Idle, BtnDown1, BtnUp1, BtnDown2 } dblclick_state;
    uint64_t dblclick_start;
    int itemAt(unsigned px, unsigned py);
    void scrollChanged(unsigned int /*position*/);
    void makeItemVisible(int item);
  public:
    ListBox(Container *parent);
    ListBox();
    ~ListBox();
    void addItem(Item const &it);
    void clearItems();
    virtual void redraw(bool no_parent_update = false);
    virtual void mouseDown(uint8_t button, MouseState mousestate);
    virtual void mouseUp(uint8_t button, MouseState mousestate);
    virtual void joyTrgDown(ui::JoyTrg trg, ui::JoyState state) override;
    virtual void joyTrgUp(ui::JoyTrg trg, ui::JoyState state) override;
    virtual void focusEnter() override;
    virtual void focusLeave() override;
    virtual bool focusable() override { return true; }
    virtual Control *getNextControl(Direction dir, Control *refctl,
                                    Point &refpt) override;
    sigc::signal<void,int> &onSelected() { return m_onSelected; }
    sigc::signal<void,int> &onDblClick() { return m_onDblClick; }
  };
}
