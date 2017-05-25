
#pragma once

#include "controls.hpp"

namespace ui {

  class Frame : public Container {
  private:
    bool m_visible;
    Point m_position;
    uint8_t m_width;
    uint8_t m_height;
    MappedSprite m_sprite;
  public:
    Frame();
    ~Frame();
    virtual uint32_t const &map(unsigned x, unsigned y) const {
	    return m_sprite.at(x,y);
    }
    virtual uint32_t &map(unsigned x, unsigned y) {
	    return m_sprite.at(x,y);
    }
    uint8_t width() const { return m_sprite.info().hsize; }
    uint8_t height() const { return m_sprite.info().vsize; }
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

}

// kate: indent-width 2; indent-mode cstyle;
