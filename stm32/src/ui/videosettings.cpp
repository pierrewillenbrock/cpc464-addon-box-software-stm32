
#include "videosettings.hpp"
#include <fpga/layout.h>

#include <unistd.h>
#include <dirent.h>

using namespace ui;

/*
  todo: need an input with up/down buttons(icon)
  todo: need a sprite with handles at the borders

  layout:
  settings:
    [vh]{blank,sync}{start,end}

    sprite screen origin and size can be calculated from blank area

    (later)[xy] pos of the iconbar
    probably should combine some of these.
    want to have a button to enter a mode where the select thing can be moved

   +------------------------------
   !        Start  End
   ! HSync  #####  #####
   ! HBlank #####  #####
   ! VSync  #####  #####
   ! VBlank #####  #####
   !             #close button#
   +------------------------------

 */

VideoSettings::VideoSettings()
  : m_hsyncStart(this)
  , m_hsyncEnd(this)
  , m_hblankStart(this)
  , m_hblankEnd(this)
  , m_vsyncStart(this)
  , m_vsyncEnd(this)
  , m_vblankStart(this)
  , m_vblankEnd(this)
  , m_start(this)
  , m_end(this)
  , m_hsync(this)
  , m_hblank(this)
  , m_vsync(this)
  , m_vblank(this)
  , m_closeButton(this)
{
  addChild(&m_hsyncStart);
  addChild(&m_hsyncEnd);
  addChild(&m_hblankStart);
  addChild(&m_hblankEnd);
  addChild(&m_vsyncStart);
  addChild(&m_vsyncEnd);
  addChild(&m_vblankStart);
  addChild(&m_vblankEnd);
  addChild(&m_start);
  addChild(&m_end);
  addChild(&m_hsync);
  addChild(&m_hblank);
  addChild(&m_vsync);
  addChild(&m_vblank);
  addChild(&m_closeButton);
  m_start.setText("Start");
  m_end.setText("End");
  m_hsync.setText("HSync");
  m_hblank.setText("HBlank");
  m_vsync.setText("VSync");
  m_vblank.setText("VBlank");
  m_closeButton.setText("Close");
  //max size: 98x71, that'd take 6958 bytes(18 bit) in vram.
  //we can spare 2048-64-24*16-19*16=1296 bytes, minus a few bytes here and
  //there. safe bet would probably be 1000 bytes. and we must remove our crap
  //if invisible.
  unsigned w = 18;
  unsigned h = 6;
  setPosition(Point(screen.rect().x + (screen.rect().width - w*8)/2,
		    screen.rect().y + (screen.rect().height - h*8)/2));
  setSize(w,h);
  m_start.setPosition(7,0);
  m_start.setSize(5,1);
  m_end.setPosition(13,0);
  m_end.setSize(5,1);
  m_hsync.setPosition(0,1);
  m_hsync.setSize(6,1);
  m_hblank.setPosition(0,2);
  m_hblank.setSize(6,1);
  m_vsync.setPosition(0,3);
  m_vsync.setSize(6,1);
  m_vblank.setPosition(0,4);
  m_vblank.setSize(6,1);

  m_hsyncStart.setPosition(7,1);
  m_hsyncStart.setSize(5,1);
  m_hsyncEnd.setPosition(13,1);
  m_hsyncEnd.setSize(5,1);
  m_hblankStart.setPosition(7,2);
  m_hblankStart.setSize(5,1);
  m_hblankEnd.setPosition(13,2);
  m_hblankEnd.setSize(5,1);
  m_vsyncStart.setPosition(7,3);
  m_vsyncStart.setSize(5,1);
  m_vsyncEnd.setPosition(13,3);
  m_vsyncEnd.setSize(5,1);
  m_vblankStart.setPosition(7,4);
  m_vblankStart.setSize(5,1);
  m_vblankEnd.setPosition(13,4);
  m_vblankEnd.setSize(5,1);

  m_closeButton.setPosition(11,5);
  m_closeButton.setSize(7,1);


  m_start.setVisible(true);
  m_end.setVisible(true);
  m_hsync.setVisible(true);
  m_hblank.setVisible(true);
  m_vsync.setVisible(true);
  m_vblank.setVisible(true);
  m_hsyncStart.setVisible(true);
  m_hsyncEnd.setVisible(true);
  m_hblankStart.setVisible(true);
  m_hblankEnd.setVisible(true);
  m_vsyncStart.setVisible(true);
  m_vsyncEnd.setVisible(true);
  m_vblankStart.setVisible(true);
  m_vblankEnd.setVisible(true);
  m_closeButton.setVisible(true);

  m_hsyncStart.setFlags(Input::Numeric);
  m_hsyncEnd.setFlags(Input::Numeric);
  m_hblankStart.setFlags(Input::Numeric);
  m_hblankEnd.setFlags(Input::Numeric);
  m_vsyncStart.setFlags(Input::Numeric);
  m_vsyncEnd.setFlags(Input::Numeric);
  m_vblankStart.setFlags(Input::Numeric);
  m_vblankEnd.setFlags(Input::Numeric);

  m_hsyncStart.setValueBounds(-1023,1023);
  m_hsyncEnd.setValueBounds(-1023,1023);
  m_hblankStart.setValueBounds(-1023,1023);
  m_hblankEnd.setValueBounds(-1023,1023);
  m_vsyncStart.setValueBounds(-312,312);
  m_vsyncEnd.setValueBounds(-312,312);
  m_vblankStart.setValueBounds(-312,312);
  m_vblankEnd.setValueBounds(-312,312);

  m_hsyncStart.onValueChanged().connect(sigc::mem_fun(this, &VideoSettings::hsyncStartChanged));
  m_hsyncEnd.onValueChanged().connect(sigc::mem_fun(this, &VideoSettings::hsyncEndChanged));
  m_hblankStart.onValueChanged().connect(sigc::mem_fun(this, &VideoSettings::hblankStartChanged));
  m_hblankEnd.onValueChanged().connect(sigc::mem_fun(this, &VideoSettings::hblankEndChanged));
  m_vsyncStart.onValueChanged().connect(sigc::mem_fun(this, &VideoSettings::vsyncStartChanged));
  m_vsyncEnd.onValueChanged().connect(sigc::mem_fun(this, &VideoSettings::vsyncEndChanged));
  m_vblankStart.onValueChanged().connect(sigc::mem_fun(this, &VideoSettings::vblankStartChanged));
  m_vblankEnd.onValueChanged().connect(sigc::mem_fun(this, &VideoSettings::vblankEndChanged));

  m_closeButton.onClick().connect(sigc::mem_fun(this, &VideoSettings::closeClicked));
}

VideoSettings::~VideoSettings() {
}

void VideoSettings::closeClicked() {
  m_onClose();
}

void VideoSettings::hsyncStartChanged(int value) {
  Screen::Options opts = screen.options();
  opts.hsync_start = value;
  screen.setOptions(opts);
}

void VideoSettings::hsyncEndChanged(int value) {
  Screen::Options opts = screen.options();
  opts.hsync_end = value;
  screen.setOptions(opts);
}

void VideoSettings::hblankStartChanged(int value) {
  Screen::Options opts = screen.options();
  opts.hblank_start = value;
  screen.setOptions(opts);
}

void VideoSettings::hblankEndChanged(int value) {
  Screen::Options opts = screen.options();
  opts.hblank_end = value;
  screen.setOptions(opts);
}

void VideoSettings::vsyncStartChanged(int value) {
  Screen::Options opts = screen.options();
  opts.vsync_start = value;
  screen.setOptions(opts);
}

void VideoSettings::vsyncEndChanged(int value) {
  Screen::Options opts = screen.options();
  opts.vsync_end = value;
  screen.setOptions(opts);
}

void VideoSettings::vblankStartChanged(int value) {
  Screen::Options opts = screen.options();
  opts.vblank_start = value;
  screen.setOptions(opts);
}

void VideoSettings::vblankEndChanged(int value) {
  Screen::Options opts = screen.options();
  opts.vblank_end = value;
  screen.setOptions(opts);
}

void VideoSettings::setVisible(bool visible) {
  Frame::setVisible(visible);
  if (visible) {
    unsigned w = 18;
    unsigned h = 6;
    setPosition(Point(screen.rect().x + (screen.rect().width - w*8)/2,
		      screen.rect().y + (screen.rect().height - h*8)/2));
    setSize(w,h);
    Screen::Options const &opts = screen.options();
    m_hsyncStart.setValue(opts.hsync_start);
    m_hsyncEnd.setValue(opts.hsync_end);
    m_hblankStart.setValue(opts.hblank_start);
    m_hblankEnd.setValue(opts.hblank_end);
    m_vsyncStart.setValue(opts.vsync_start);
    m_vsyncEnd.setValue(opts.vsync_end);
    m_vblankStart.setValue(opts.vblank_start);
    m_vblankEnd.setValue(opts.vblank_end);
  } else {
  }
}

