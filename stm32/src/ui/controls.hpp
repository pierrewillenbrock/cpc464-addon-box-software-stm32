
#pragma once

#include <ui/ui.hpp>
#include <fpga/sprite.hpp>
#include <fpga/fpga_uploader.hpp>
#include <ui/icons.hpp>

#include <string>
#include <vector>
#include <list>
#include <sigc++/sigc++.h>

namespace ui {
class SubControl;

class Container : public virtual MappedControl {
protected:
  std::list<SubControl*> m_children;
  friend class SubControl;
public:
  virtual void fullRedraw() = 0;
  virtual void updatedMap() = 0;
  virtual bool isMapped() = 0;
  virtual Control *getChildAt(Point p);
  void addChild(SubControl *ctl);
  void removeChild(SubControl *ctl);
  virtual Control *getNextControl(Direction dir, Control *refctl,
                                  Point &refpt);
};

class SubControl : public virtual MappedControl {
protected:
  uint8_t m_x;
  uint8_t m_y;
  uint8_t m_width;
  uint8_t m_height;
  bool m_visible;
  Container *m_parent;
  unsigned m_taborder;
  virtual uint32_t const &map(unsigned x, unsigned y) const {
    assert(m_parent);
    return m_parent->map(x + m_x, y + m_y);
  }
  virtual uint32_t &map(unsigned x, unsigned y) {
    assert(m_parent);
    return m_parent->map(x + m_x, y + m_y);
  }
public:
  SubControl(Container *parent);
  SubControl();
  void setPosition(unsigned x, unsigned y);
  void setSize(unsigned width, unsigned height);
  virtual void setVisible(bool visible);
  bool visible() { return m_visible; }
  virtual void redraw(bool no_parent_update = false) = 0;
  virtual Rect getRect();
  virtual Rect getGlobalRect();
  virtual void unmapped() {}
  virtual void mapped() {}
  virtual void setParent(Container *parent);
  virtual Control *getNextControl(Direction dir, Control *refctl,
                                  Point &refpt);
  virtual void setTabOrder(unsigned taborder) { m_taborder = taborder; }
  virtual unsigned tabOrder() const { return m_taborder; }
  virtual bool focusable() { return false; }
};

  class Panel : public Container, public SubControl {
  public:
    Panel(Container *parent);
    Panel();
    ~Panel();
    //in tile position, relative to the parent
    virtual void fullRedraw();
    virtual void updatedMap();
    virtual void redraw(bool no_parent_update = false);
    virtual void unmapped();
    virtual void mapped();
    virtual void setVisible(bool visible);
    virtual bool isMapped() { return m_parent->isMapped() && m_visible; }
  };

  class Button : public SubControl {
  private:
    std::string m_text;
    RefPtr<Icon const> m_icon;
    bool m_pressed;
    sigc::signal<void> m_onClick;
  public:
    Button(Container *parent);
    Button();
    ~Button();
    void setText(std::string const &text);
    void setIcon(RefPtr<Icon const> const &icon);
    virtual void redraw(bool no_parent_update = false);
    virtual void mouseDown(uint8_t button, MouseState mousestate);
    virtual void mouseUp(uint8_t button, MouseState mousestate);
    sigc::signal<void> &onClick() { return m_onClick; }
  };

  class Label : public SubControl {
  private:
    std::string m_text;
  public:
    Label(Container *parent);
    Label();
    ~Label();
    void setText(std::string const &text);
    virtual void redraw(bool no_parent_update = false);
  };

}

// kate: indent-width 2; indent-mode cstyle; replace-tabs on;
