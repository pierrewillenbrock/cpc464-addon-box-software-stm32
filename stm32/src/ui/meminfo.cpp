
#include "meminfo.hpp"
#include <sys/mprot.h>
#include <sys/info.h>
#include <fpga/sprite.h>
#include <malloc.h>

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

MemInfo::MemInfo()
  : m_closeButton(this)
{
  for(auto &il : infolines) {
    il.label.setParent(this);
    il.input.setParent(this);
    il.input.setFlags(Input::Numeric);
    addChild(&il.label);
    addChild(&il.input);
    il.label.setVisible(true);
    il.input.setVisible(true);
  }
  addChild(&m_closeButton);
  m_closeButton.setText("Close");

  m_closeButton.setVisible(true);

  m_closeButton.onClick().connect(sigc::mem_fun(this, &MemInfo::closeClicked));

  infolines[0].label.setText("Sys: Total heap space available");
  infolines[1].label.setText("Sys: Unclaimed heap space");
  infolines[2].label.setText("Mem: Space allocated from system");
  infolines[3].label.setText("Mem: Number of non-inuse chunks");
  infolines[4].label.setText("Mem: Total allocated space");
  infolines[5].label.setText("Mem: Total non-inuse space");
  infolines[6].label.setText("Mem: Releasable space");
  infolines[7].label.setText("MProt: Buckets used");
  infolines[8].label.setText("MProt: Buckets used by owner");
  infolines[9].label.setText("MProt: Maximum list depth");
  infolines[10].label.setText("MProt: Average list depth");
  infolines[11].label.setText("Gfx: Memory available");
  infolines[12].label.setText("Gfx: Memory used");
  infolines[13].label.setText("Gfx: Largest block available");

  unsigned w = 40;
  unsigned h = infolines.size()+1;
  setSize(w,h);  
  setPosition(Point(screen.rect().x + (screen.rect().width - w*8)/2,
        screen.rect().y + (screen.rect().height - h*8)/2));

  for(unsigned int i = 0; i < infolines.size(); i++) {
    infolines[i].label.setPosition(0,i);
    infolines[i].label.setSize(32,1);
    infolines[i].input.setPosition(32,i);
    infolines[i].input.setSize(8,1);
  }
  m_closeButton.setPosition(33,infolines.size());
  m_closeButton.setSize(7,1);

}

MemInfo::~MemInfo() {
}

void MemInfo::closeClicked() {
  m_onClose();
}

void MemInfo::setVisible(bool visible) {
  if (visible) {
    unsigned w = 40;
    unsigned h = infolines.size()+1;
    setSize(w,h);  
    setPosition(Point(screen.rect().x + (screen.rect().width - w*8)/2,
		      screen.rect().y + (screen.rect().height - h*8)/2));

    struct mallinfo mainfo = mallinfo();
    struct MProtInfo mpinfo = mprot_info();
    struct SysMemInfo sysinfo = sysmeminfo();
    struct SpriteVMemInfo spriteinfo = spritevmeminfo();
    infolines[0].input.setValue(sysinfo.total);
    infolines[1].input.setValue(sysinfo.free);
    infolines[2].input.setValue(mainfo.arena);
    infolines[3].input.setValue(mainfo.ordblks);
    infolines[4].input.setValue(mainfo.uordblks);
    infolines[5].input.setValue(mainfo.fordblks);
    infolines[6].input.setValue(mainfo.keepcost);
    infolines[7].input.setValue(mpinfo.buckets_used);
    infolines[8].input.setValue(mpinfo.buckets_used_by_owner);
    infolines[9].input.setValue(mpinfo.max_list_depth);
    infolines[10].input.setValue(mpinfo.average_list_depth);
    infolines[11].input.setValue(spriteinfo.total);
    infolines[12].input.setValue(spriteinfo.used);
    infolines[13].input.setValue(spriteinfo.largestFreeBlock);
  }
  Frame::setVisible(visible);
}

// kate: indent-width 2; indent-mode cstyle;
