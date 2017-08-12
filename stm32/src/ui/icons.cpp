
#include <ui/icons.hpp>

#include <fpga/sprite.h>
#include <fpga/layout.h>
#include <string.h>

using namespace ui;

namespace ui {
  Icons icons;
}

/* icons: Folder:

    0 1 2 3 4 5 6 7
  0 ########
  1 ##############
  2 ##############
  3 ######      ##
  4 ##          ##
  5 ##          ##
  6 ##############
  7 

   File:
    0 1 2 3 4 5 6 7
  0   ########
  1   ##    ####  
  2   ##    ######
  3   ##        ##
  4   ##        ##
  5   ##        ##
  6   ############
  7               

   New Folder:
    0 1 2 3 4 5 6 7
  0 ########
  1 ##############
  2 ##############
  3 ######      ##
  4 ##          
  5 ##          ##
  6 ########  ######
  7             ##

   Up Down:
    0 1 2 3 4 5 6 7
  0       ##
  1     ######    
  2   ##########  
  3       ##      
  4   ##########
  5     ######    
  6       ##        
  7               


 */

uint8_t const iconmap[][8] = {
  { 0xf0, 0xfe, 0xfe, 0xe2, 0x82, 0x82, 0xfe, 0x00 },
  { 0x78, 0x4c, 0x4e, 0x42, 0x42, 0x42, 0x7e, 0x00 },
  { 0xf0, 0xfe, 0xfe, 0xe2, 0x80, 0x82, 0xf7, 0x02 },
  { 0x10, 0x38, 0x7c, 0x10, 0x7c, 0x38, 0x10, 0x00 },
};

void Icon::allocate() const {
  icons.allocateIcon(this);
}

void Icon::deallocate() const {
  icons.deallocateIcon(this);
}

Icons::Icons()
  : uploading(0xff)
{
}

void Icons::allocateIcon(Icon const *icon) {
  unsigned iconno = 0;
  for(iconno = 0; iconno < m_icons.size(); iconno++) {
    if (&m_icons[iconno] == icon)
      break;
  }
  assert(iconno < m_icons.size());
  if (iconno >= m_icons.size())
    return;
  //find a space in one of the tiles
  for(auto &ti : m_icontiles) {
    for(unsigned i = 0; i < 4; i++) {
      if (ti.assigned[i] == 0xff) {
	//found. now assign and flag.
	if (ti.addr == 0xffff)
	  ti.addr = sprite_alloc_vmem(0x10, 0x10, ~0U);
	ti.assigned[i] = iconno;
	ti.dirty = true;
	m_icons[iconno].def_map = (2 << 21) | (15 << 17) | 0x10000 | ((ti.addr & 0x7f0) >> 2) | i;
	m_icons[iconno].sel_map = (1 << 21) | (15 << 17) | 0x10000 | ((ti.addr & 0x7f0) >> 2) | i;
	icons.checkTiles();
	return;
      }
    }
  }
}

void Icons::deallocateIcon(Icon const *icon) {
  //using the map to find the address
  uint16_t addr = (icon->def_map << 2) & 0x7f0;
  //looking through the tiles to find the one with the address
  for(auto &ti : m_icontiles) {
    if (ti.addr == addr) {
      //using the next 2 bits to determine the entry used and invalidate.
      ti.assigned[icon->def_map & 0x3] = 0xff;
      if (ti.assigned[0] == 0xff && ti.assigned[1] == 0xff &&
	  ti.assigned[2] == 0xff && ti.assigned[3] == 0xff) {
	sprite_free_vmem(ti.addr);
	ti.addr = 0xffff;
	ti.dirty = false;
      }
      break;
    }
  }
}

void Icons::fpgacmpl(int result) {
  if (result != 0) {
    FPGAComm_ReadWriteCommand(&fpgacmd);
    return;
  }
  icons.uploading = 0xff;
  icons.checkTiles();
}

void Icons::checkTiles() {
  if (uploading != 0xff)
    return;
  for(unsigned int i = 0; i < m_icontiles.size(); i++) {
    auto &ti = m_icontiles[i];
    if (ti.dirty) {
      uploading = i;
      memset(tiledata, 0, sizeof(tiledata));
      for(unsigned int j = 0; j < 8; j++) {
	uint8_t d0 = (ti.assigned[0] != 0xff)?iconmap[ti.assigned[0]][j]:0;
	uint8_t d1 = (ti.assigned[1] != 0xff)?iconmap[ti.assigned[1]][j]:0;
	uint8_t d2 = (ti.assigned[2] != 0xff)?iconmap[ti.assigned[2]][j]:0;
	uint8_t d3 = (ti.assigned[3] != 0xff)?iconmap[ti.assigned[3]][j]:0;
	tiledata[j*2+0]=
	  ((d0 & 0x80) >> 7) |
	  ((d1 & 0x80) >> 6) |
	  ((d2 & 0x80) >> 5) |
	  ((d3 & 0x80) >> 4) |
	  ((d0 & 0x40) >> 2) |
	  ((d1 & 0x40) >> 1) |
	  ((d2 & 0x40) >> 0) |
	  ((d3 & 0x40) << 1) |
	  ((d0 & 0x20) << 11) |
	  ((d1 & 0x20) << 12) |
	  ((d2 & 0x20) << 13) |
	  ((d3 & 0x20) << 14) |
	  ((d0 & 0x10) << 16) |
	  ((d1 & 0x10) << 17) |
	  ((d2 & 0x10) << 18) |
	  ((d3 & 0x10) << 19);
	tiledata[j*2+1] =
	  ((d0 & 0x08) >> 3) |
	  ((d1 & 0x08) >> 2) |
	  ((d2 & 0x08) >> 1) |
	  ((d3 & 0x08) >> 0) |
	  ((d0 & 0x04) << 2) |
	  ((d1 & 0x04) << 3) |
	  ((d2 & 0x04) << 4) |
	  ((d3 & 0x04) << 5) |
	  ((d0 & 0x02) << 15) |
	  ((d1 & 0x02) << 16) |
	  ((d2 & 0x02) << 17) |
	  ((d3 & 0x02) << 18) |
	  ((d0 & 0x01) << 20) |
	  ((d1 & 0x01) << 21) |
	  ((d2 & 0x01) << 22) |
	  ((d3 & 0x01) << 23);
      }
      ti.dirty = false;
      if (ti.addr != 0xffff) {
	fpgacmd.read_data = NULL;
	fpgacmd.slot = sigc::mem_fun(this, &Icons::fpgacmpl);
	fpgacmd.address = FPGA_GRPH_SPRITES_RAM + 4*ti.addr;
	fpgacmd.length = sizeof(tiledata);
	fpgacmd.write_data = tiledata;
	FPGAComm_ReadWriteCommand(&fpgacmd);
      }
    }
  }
}

RefPtr<Icon const> Icons::getIcon(IconName name) {
  unsigned num = (unsigned)name;
  assert(num < m_icons.size());
  if (num >= m_icons.size())
    return RefPtr<Icon const>();
  return &m_icons[num];
}
