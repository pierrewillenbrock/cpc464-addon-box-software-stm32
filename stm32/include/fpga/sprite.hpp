
#pragma once

#include <fpga/sprite.h>

#include <vector>

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
protected:
  virtual bool allocateMap(sprite_info &) { return true; }
  virtual void freeMap(sprite_info &) {}
public:
  Sprite();
  Sprite(Sprite const &sp);
  Sprite &operator=(Sprite const &sp);
  virtual ~Sprite();
  // higher zorder gets ordered on top of lower.
  /** \brief Set the Z-Order of this sprite
   *
   * Sets the Z-Order of this sprite. Higher zorder sprites get layered on top.
   * \param zorder  Z-Order of this sprite. Higher if in front.
   */
  void setZOrder(unsigned zorder);
  /** \brief Set priority of this sprite
   *
   * Sets the priority of this sprite. Higher ist more important.
   * \param priority  Priority of the sprite. Higher ist more important.
   */
  void setPriority(unsigned priority);
  // this trys to make the given sprite visible, but may fail at that.
  // check isAllocated if you need to know if it is actually visible.
  void setVisible(bool visible);
  void setSpriteInfo(struct sprite_info const &info);
  sprite_info const &info() const { return m_info; }
  bool isAllocated();
  /** \brief Upload map data
   * This function uploads the tilemap located at data to the fpga memory
   * address and size setup in the sprite_info.
   * The caller is responsible to keep data valid for a while.
   * \param data  Pointer to the data to be uploaded
   */
  void triggerMapUpload(uint32_t *data);
};

class MappedSprite: public Sprite {
private:
  std::vector<uint32_t> storage;
  uint16_t map_addr;
protected:
  virtual bool allocateMap(sprite_info &i);
  virtual void freeMap(sprite_info &i);
public:
  MappedSprite();
  MappedSprite(MappedSprite const &sp);
  MappedSprite &operator=(MappedSprite const &sp);
  virtual ~MappedSprite();
  uint32_t const &at(unsigned x, unsigned y) const;
  uint32_t &at(unsigned x, unsigned y);
  void updateDone();
  void setSize(unsigned x, unsigned y);
  void setPosition(unsigned x, unsigned y);
  void setDoubleSize(bool doublesize);
};

void Sprite_Setup();

// kate: indent-width 2; indent-mode cstyle;
