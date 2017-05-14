
#pragma once

#include <ui/ui.hpp>
#include <fpga/sprite.hpp>
#include <ui/icons.hpp>

#include <string>
#include <vector>
#include <list>
#include <sigc++/sigc++.h>

namespace ui {
  class SubControl;

  class Container : public virtual Control {
  protected:
    std::list<SubControl*> m_children;
  public:
    virtual uint32_t *map() = 0;
    virtual unsigned mapPitch() = 0;
    virtual void fullRedraw() = 0;
    virtual void updatedMap() = 0;
    virtual bool isMapped() = 0;
    //this can and is expected to be the same as ui::Control::getGlobalRect
    virtual Control *getChildAt(Point p);
    void addChild(SubControl *ctl) {
      m_children.push_back(ctl);
      fullRedraw();
    }
    void removeChild(SubControl *ctl) {
      for(auto it = m_children.begin(); it != m_children.end(); it++) {
	if (*it == ctl) {
	  m_children.erase(it);
	  break;
	}
      }
      fullRedraw();
    }
  };

  class Frame : public Container {
  private:
    bool m_visible;
    Point m_position;
    uint8_t m_width;
    uint8_t m_height;
    sprite_info m_spriteinfo;
    std::vector<uint32_t> m_map;
    Sprite m_sprite;
    FPGA_Uploader map_uploader;
  public:
    Frame();
    ~Frame();
    virtual uint32_t *map() { return m_map.size() == 0?NULL:m_map.data(); }
    virtual unsigned mapPitch() { return m_spriteinfo.hpitch; }
    uint8_t width() { return m_width; }
    uint8_t height() { return m_height; }
    virtual void fullRedraw();
    virtual void updatedMap();
    //in pixels
    void setPosition(Point p);
    //in tiles
    void setSize(unsigned width, unsigned height);
    virtual void setVisible(bool visible);
    bool visible() { return m_visible; }
    virtual Rect getRect();
    virtual Rect getGlobalRect();
    virtual bool isMapped() { return m_visible; }
  };

  class SubControl : public virtual Control {
  protected:
    uint8_t m_x;
    uint8_t m_y;
    uint8_t m_width;
    uint8_t m_height;
    bool m_visible;
    Container *m_parent;
  public:
    SubControl(Container *parent);
    SubControl();
    void setPosition(unsigned x, unsigned y);
    void setSize(unsigned width, unsigned height);
    virtual void setVisible(bool visible);
    bool visible() { return m_visible; }
    virtual void redraw() = 0;
    virtual Rect getRect();
    virtual Rect getGlobalRect();
    virtual void unmapped() {}
    virtual void mapped() {}
    virtual void setParent(Container *parent);
  };

  class Panel : public Container, public SubControl {
  public:
    Panel(Container *parent);
    Panel();
    ~Panel();
    //in tile position, relative to the parent
    virtual uint32_t *map();
    virtual unsigned mapPitch();
    virtual void fullRedraw();
    virtual void updatedMap();
    virtual void redraw();
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
    virtual void redraw();
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
    virtual void redraw();
  };

}
