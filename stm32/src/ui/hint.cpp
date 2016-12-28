
#include "hint.hpp"
#include <fpga/font.h>
#include <fpga/layout.h>

using namespace ui;

Hint::Hint() {
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

Hint::~Hint() {
  if (m_spriteinfo.map_addr != 65535)
    sprite_free_vmem(m_spriteinfo.map_addr);
}

void Hint::setPosition(Point p) {
  m_spriteinfo.hpos = p.x;
  m_spriteinfo.vpos = p.y;
  m_sprite.setSpriteInfo(m_spriteinfo);
}

void Hint::setText(std::string const &text) {
  m_text = text;
  m_spriteinfo.hsize = m_text.size();
  m_spriteinfo.hpitch = m_text.size();

  if (m_visible) {
    if (m_spriteinfo.map_addr != 65535) {
      sprite_free_vmem(m_spriteinfo.map_addr);
      m_spriteinfo.map_addr = 65535;
    }
    unsigned addr = sprite_alloc_vmem(m_text.size(), 1, ~0U);
    if (addr != ~0U) {
      m_spriteinfo.map_addr = addr;
      map.resize(m_text.size());
      for(unsigned i = 0; i < m_text.size(); i++) {
	map[i] = font_get_tile(m_text[i], 15, 1);
      }
      map_uploader.setSrc(map.data());
      map_uploader.setDest(FPGA_GRPH_SPRITES_RAM + addr*4);
      map_uploader.setSize(map.size()*4);
      map_uploader.triggerUpload();
      m_sprite.setSpriteInfo(m_spriteinfo);
    }
  }
}

void Hint::setVisible(bool visible) {
  if (m_visible == visible)
    return;
  m_visible = visible;
  if (!visible) {
    if (m_spriteinfo.map_addr != 65535) {
      sprite_free_vmem(m_spriteinfo.map_addr);
      m_spriteinfo.map_addr = 65535;
    }
  } else {
    unsigned addr = sprite_alloc_vmem(m_text.size(), 1, ~0U);
    if (addr != ~0U) {
      m_spriteinfo.map_addr = addr;
      map.resize(m_text.size());
      for(unsigned i = 0; i < m_text.size(); i++) {
	map[i] = font_get_tile(m_text[i], 15, 1);
      }
      map_uploader.setSrc(map.data());
      map_uploader.setDest(FPGA_GRPH_SPRITES_RAM + addr*4);
      map_uploader.setSize(map.size()*4);
      map_uploader.triggerUpload();
      m_sprite.setSpriteInfo(m_spriteinfo);
    }
  }
  m_sprite.setVisible(m_visible);
}

