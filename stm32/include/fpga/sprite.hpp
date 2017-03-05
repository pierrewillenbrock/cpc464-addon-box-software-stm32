
#pragma once

#include <fpga/sprite.h>
#include <fpga/fpga_uploader.hpp>

class Sprite {
private:
  bool m_visible;
  int m_allocated;
  unsigned m_zorder;
  unsigned m_priority;
  sprite_info m_info;
  void triggerUpload();
  static void checkAllocations();
  static void doRegister(Sprite *sprite);
  static void unregister(Sprite *sprite);
public:
  Sprite();
  Sprite(Sprite const &sp);
  Sprite &operator=(Sprite const &sp);
  ~Sprite();
  // higher zorder gets ordered on top of lower.
  void setZOrder(unsigned zorder);
  // higher priority gets allocated in preference of lower.
  void setPriority(unsigned priority);
  // this trys to make the given sprite visible, but may fail at that.
  // check isAllocated if you need to know if it is actually visible.
  void setVisible(bool visible);
  void setSpriteInfo(struct sprite_info const &info);
  sprite_info const &info() { return m_info; }
  bool isAllocated();
};

void Sprite_Setup();
