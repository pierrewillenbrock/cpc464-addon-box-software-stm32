
#include "controls.hpp"
#include <fpga/font.h>
#include <fpga/layout.h>

using namespace ui;

Frame::Frame() {
  m_spriteinfo.hpos = 65520;
  m_spriteinfo.vpos = 65520;
  m_spriteinfo.map_addr = 65535;
  m_spriteinfo.hsize = 0;
  m_spriteinfo.vsize = 1;
  m_spriteinfo.hpitch = 0;
  m_spriteinfo.doublesize = 0;
  m_sprite.setPriority(2);
  m_sprite.setZOrder(50);
}

Frame::~Frame() {
  if (m_spriteinfo.map_addr != 65535)
    sprite_free_vmem(m_spriteinfo.map_addr);
}

Rect Frame::getRect() {
  Rect r = {
    m_position.x, m_position.y, (uint16_t)(m_width*8), (uint16_t)(m_height *8)
  };
  return r;
}

Rect Frame::getGlobalRect() {
  return getRect();
}

void Frame::fullRedraw() {
  if (!m_visible)
    return;
  for(auto &tile : m_map)
    tile = font_get_tile(' ',15, 1);
  for(auto &ch : m_children)
    ch->redraw();
}

void Frame::updatedMap() {
  unsigned addr = m_spriteinfo.map_addr;
  if (addr != 65535) {
    map_uploader.setSrc(m_map.data());
    map_uploader.setDest(FPGA_GRPH_SPRITES_RAM + addr*4);
    map_uploader.setSize(m_map.size()*4);
    map_uploader.triggerUpload();
  }
}

void Frame::setPosition(Point p) {
  m_position = p;
  m_spriteinfo.hpos = p.x;
  m_spriteinfo.vpos = p.y;
  m_sprite.setSpriteInfo(m_spriteinfo);
}

void Frame::setSize(unsigned width, unsigned height) {
  m_spriteinfo.hsize = width;
  m_spriteinfo.hpitch = width;
  m_spriteinfo.vsize = height;
  m_width = width;
  m_height = height;
  if (m_visible) {
    if (m_spriteinfo.map_addr != 65535) {
      sprite_free_vmem(m_spriteinfo.map_addr);
      m_spriteinfo.map_addr = 65535;
    }
    unsigned addr = sprite_alloc_vmem(m_spriteinfo.hpitch*m_spriteinfo.vsize,
				      1, ~0U);
    if (addr != ~0U) {
      m_spriteinfo.map_addr = addr;
      m_map.resize(m_spriteinfo.hpitch*m_spriteinfo.vsize);
      fullRedraw();
      m_sprite.setSpriteInfo(m_spriteinfo);
    } else {
      m_map.clear();
    }
  }
}

void Frame::setVisible(bool visible) {
  if (m_visible == visible)
    return;
  m_visible = visible;
  if (!visible) {
    if (m_spriteinfo.map_addr != 65535) {
      sprite_free_vmem(m_spriteinfo.map_addr);
      m_spriteinfo.map_addr = 65535;
      m_map.clear();
    }
  } else {
    m_map.resize(m_spriteinfo.hpitch*m_spriteinfo.vsize);
    unsigned addr = sprite_alloc_vmem(m_map.size(), 1, ~0U);
    if (addr != ~0U) {
      m_spriteinfo.map_addr = addr;
      fullRedraw();
      m_sprite.setSpriteInfo(m_spriteinfo);
    } else {
      m_map.clear();
    }
  }
  m_sprite.setVisible(m_visible);
}

