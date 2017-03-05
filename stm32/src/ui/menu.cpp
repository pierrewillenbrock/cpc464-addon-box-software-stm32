
#include <fpga/font.h>
#include "menu.hpp"
#include <fpga/font.h>
#include <fpga/layout.h>

using namespace ui;

Menu::Menu() {
  m_spriteinfo.hpos = 65520;
  m_spriteinfo.vpos = 65520;
  m_spriteinfo.map_addr = 65535;
  m_spriteinfo.hsize = 0;
  m_spriteinfo.vsize = 1;
  m_spriteinfo.hpitch = 0;
  m_spriteinfo.doublesize = 0;
  m_sprite.setPriority(30);
  m_sprite.setZOrder(40);
  mouse_over_item = -1;
}

Menu::~Menu() {
  if (m_spriteinfo.map_addr != 65535)
    sprite_free_vmem(m_spriteinfo.map_addr);
}

//just the corner position. gets adjusted to fit on screen
void Menu::setPosition(Point p) {
  m_position = p;
  m_spriteinfo.hpos = m_position.x;
  m_spriteinfo.vpos = m_position.y;
  if (m_spriteinfo.hpos + m_spriteinfo.hsize*8 >
      screen.rect().x+screen.rect().width)
    m_spriteinfo.hpos = p.x-m_spriteinfo.hsize*8;
  if (m_spriteinfo.vpos + m_spriteinfo.vsize*8 >
      screen.rect().y+screen.rect().height)
    m_spriteinfo.vpos = p.y-m_spriteinfo.vsize*8;
  m_sprite.setSpriteInfo(m_spriteinfo);
}

void Menu::generateMap() {
  unsigned max_len = 0;
  for(unsigned i = 0; i < getItemCount(); i++) {
    if (max_len < getItemText(i).size())
      max_len = getItemText(i).size();
  }
  m_spriteinfo.hsize = max_len;
  m_spriteinfo.hpitch = m_spriteinfo.hsize;
  m_spriteinfo.vsize = getItemCount();
  map.resize(m_spriteinfo.hsize * m_spriteinfo.vsize);
  for(unsigned i = 0; i < getItemCount(); i++) {
    unsigned int pal = 15;
    if ((int)i == mouse_over_item) {
      pal = 11;
    }
    std::string text = getItemText(i);
    unsigned j = 0;
    for(; j < text.size(); j++)
      map[i*m_spriteinfo.hpitch+j] = font_get_tile(text[j],pal, 1);
    for(; j < m_spriteinfo.hsize; j++)
      map[i*m_spriteinfo.hpitch+j] = font_get_tile(' ',pal, 1);
  }
  m_spriteinfo.hpos = m_position.x;
  m_spriteinfo.vpos = m_position.y;
  if (m_spriteinfo.hpos + m_spriteinfo.hsize*8 >
      screen.rect().x+screen.rect().width)
    m_spriteinfo.hpos = m_position.x-m_spriteinfo.hsize*8;
  if (m_spriteinfo.vpos + m_spriteinfo.vsize*8 >
      screen.rect().y+screen.rect().height)
    m_spriteinfo.vpos = m_position.y-m_spriteinfo.vsize*8;
  if (m_spriteinfo.map_addr != 65535) {
    map_uploader.setSrc(map.data());
    map_uploader.setDest(FPGA_GRPH_SPRITES_RAM + m_spriteinfo.map_addr*4);
    map_uploader.setSize(map.size()*4);
    map_uploader.triggerUpload();
    m_sprite.setSpriteInfo(m_spriteinfo);
  }
}

Rect Menu::getRect() {
  Rect r;
  r.x = m_spriteinfo.hpos;
  r.y = m_spriteinfo.vpos;
  r.width = m_spriteinfo.hsize*8;
  r.height = m_spriteinfo.vsize*8;
  return r;
}

Rect Menu::getGlobalRect() {
  return getRect();
}

void Menu::mouseDown(uint8_t button, MouseState mousestate) {
  ui::Rect r = getGlobalRect();
  if (mousestate.x < r.x ||
      mousestate.y < r.y ||
      mousestate.x >= r.x+r.width ||
      mousestate.y >= r.y+r.height) {
    if (button == 0 && mousestate.buttons == 1)
      pressed_item = -1;
    return;
  }
  if (button == 0 && mousestate.buttons == 1)
    pressed_item = (mousestate.y - r.y)/8;
}

void Menu::mouseUp(uint8_t button, MouseState mousestate) {
  ui::Rect r = getGlobalRect();
  if (mousestate.x < r.x ||
      mousestate.y < r.y ||
      mousestate.x >= r.x+r.width ||
      mousestate.y >= r.y+r.height) {
    if (button == 0 && mousestate.buttons == 0) {
      if (pressed_item == -1)
	selectItem(-1);
    }
    return;
  }
  if (button == 0 && mousestate.buttons == 0) {
    int itemno = (mousestate.y - r.y)/8;
    if (itemno == pressed_item) {
      selectItem(itemno);
    }
  }
}

void Menu::mouseMove(int16_t /*dx*/, int16_t /*dy*/, MouseState mousestate) {
  ui::Rect r = getRect();
  if (mousestate.x < r.x ||
      mousestate.y < r.y ||
      mousestate.x >= r.x+r.width ||
      mousestate.y >= r.y+r.height ||
      mousestate.buttons) {
    if (mouse_over_item != -1) {
      mouse_over_item = -1;
      generateMap();
    }
    return;
  }
  int itemno = (mousestate.y - r.y)/8;
  if (itemno != mouse_over_item) {
    mouse_over_item = itemno;
    generateMap();
  }
}

void Menu::setVisible(bool visible) {
  if (m_visible == visible)
    return;
  m_visible = visible;
  if (!visible) {
    if (m_spriteinfo.map_addr != 65535) {
      sprite_free_vmem(m_spriteinfo.map_addr);
      m_spriteinfo.map_addr = 65535;
    }
  } else {
    m_spriteinfo.map_addr = 65535;
    generateMap();
    unsigned addr = sprite_alloc_vmem(map.size(), 1, ~0U);
    if (addr != ~0U) {
      m_spriteinfo.map_addr = addr;
      map_uploader.setSrc(map.data());
      map_uploader.setDest(FPGA_GRPH_SPRITES_RAM + m_spriteinfo.map_addr*4);
      map_uploader.setSize(map.size()*4);
      map_uploader.triggerUpload();
      m_sprite.setSpriteInfo(m_spriteinfo);
    }
  }
  m_sprite.setVisible(m_visible);
}

