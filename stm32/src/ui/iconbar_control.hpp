
#pragma once

#include <ui/ui.hpp>
#include <fpga/sprite.hpp>
#include "hint.hpp"

class IconBar_Control : public ui::MappedControl {
private:
  MappedSprite m_sprite;
  ui::Hint hint;
  int hintforicon;
  int mouse_press_iconno;
  int keyjoy_sel_iconno;

  std::string diskHint[4];
  std::string joystickHint[2];
  std::string mouseHint;
  std::string lpenHint;

  uint8_t tile_disktlno;
  uint8_t tile_disktrno;
  uint8_t tile_diskblno;
  uint8_t tile_diskbrno;
  uint8_t tile_joytlno;
  uint8_t tile_joytrno;
  uint8_t tile_joyblno;
  uint8_t tile_joybrno;
  uint8_t tile_mousetlno;
  uint8_t tile_mousetrno;
  uint8_t tile_mouseblno;
  uint8_t tile_mousebrno;
  uint8_t tile_lpentlno;
  uint8_t tile_lpentrno;
  uint8_t tile_lpenblno;
  uint8_t tile_lpenbrno;
  uint8_t tile_settingstlno;
  uint8_t tile_settingstr_blno;
  uint8_t tile_settingsbrno;

  FPGA_Uploader mousetr_uploader;
  FPGA_Uploader lpentr_uploader;

  unsigned disk_motor_anim;
  uint32_t diskMotor_timer;

  void diskMotorTimeout();

  void showHint(int iconno, unsigned int x);
public:
  IconBar_Control()
    : hintforicon(-1)
    , mouse_press_iconno(-1)
    , keyjoy_sel_iconno(-1)
    , diskMotor_timer(0) {
  }
  void screenRectChange(ui::Rect const &r) {
    m_sprite.setPosition(r.x + r.width - 18*8, r.y + r.height - 2*8);
  }
  void init();
  virtual uint32_t const &map(unsigned x, unsigned y) const {
    return m_sprite.at(x,y);
  }
  virtual uint32_t &map(unsigned x, unsigned y) {
    return m_sprite.at(x,y);
  }
  virtual ui::Rect getRect() {
    sprite_info const &info = m_sprite.info();
    ui::Rect r = {
      .x = info.hpos,
      .y = info.vpos,
      .width = (uint16_t)(info.hsize*8),
      .height = (uint16_t)(info.vsize*8)
    };
    return r;
  }
  virtual ui::Rect getGlobalRect() {
    return getRect();
  }
  void updateDone() { m_sprite.updateDone(); }
  virtual void mouseDown(uint8_t button, ui::MouseState mousestate);
  virtual void mouseUp(uint8_t button, ui::MouseState mousestate);
  virtual void mouseMove(int16_t /*dx*/, int16_t /*dy*/, ui::MouseState mousestate);
  virtual void joyTrgDown(ui::JoyTrg trg, ui::JoyState state);
  virtual void joyTrgUp(ui::JoyTrg trg, ui::JoyState state);
  //we are the top level, so we get all events. mouseleave never happens.
  virtual void diskSelected(unsigned diskno, ui::Point where) = 0;
  virtual void joystickSelected(unsigned joyno, ui::Point where) = 0;
  virtual void mouseSelected(ui::Point where) = 0;
  virtual void lpenSelected(ui::Point where) = 0;
  virtual void settingsSelected(ui::Point where) = 0;

  void setDiskActivity(unsigned no, bool activity);
  void setDiskAssigned(unsigned no, bool assigned);
  void setDiskMotor(bool on);
  void setJoystickAssigned(unsigned no, bool assigned);
  void setMouseAssigned(bool assigned);
  void setLpenAssigned(bool assigned);

  void setDiskHint(unsigned no, std::string const &hint);
  void setJoystickHint(unsigned no, std::string const &hint);
  void setMouseHint(std::string const &hint);
  void setLpenHint(std::string const &hint);
};

