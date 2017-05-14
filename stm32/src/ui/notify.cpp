
#include <ui/notify.hpp>
#include <ui/ui.hpp>
#include <fpga/fpga_uploader.hpp>
#include <fpga/sprite.hpp>
#include <fpga/font.h>
#include <fpga/layout.h>
#include <vector>
#include <deque>
#include <deferredwork.hpp>
#include <timer.h>
#include <ui/icons.hpp>

using namespace ui;

struct Notification {
	Icon *icon;
	std::string message;
};

static sprite_info notify_spriteinfo;
static std::vector<uint32_t> notify_map;
static Sprite notify_sprite;
static FPGA_Uploader notify_map_uploader;
static std::deque<Notification> notify_list;
static bool notify_updateQueued = false;

void ui::Notification_Setup() {
	notify_spriteinfo.hpos = 65520;
	notify_spriteinfo.vpos = 65520;
	notify_spriteinfo.map_addr = 65535;
	notify_spriteinfo.hsize = 0;
	notify_spriteinfo.vsize = 1;
	notify_spriteinfo.hpitch = 0;
	notify_spriteinfo.doublesize = 0;
	notify_sprite.setPriority(5);
	notify_sprite.setZOrder(40);
}

static void Notify_updatedMap() {
	unsigned addr = notify_spriteinfo.map_addr;
	if (addr != 65535) {
		notify_map_uploader.setSrc(notify_map.data());
		notify_map_uploader.setDest(FPGA_GRPH_SPRITES_RAM + addr*4);
		notify_map_uploader.setSize(notify_map.size()*4);
		notify_map_uploader.triggerUpload();
	}
}

static void Notify_Redraw() {
	for(auto &tile : notify_map)
		tile = font_get_tile(' ',15, 1);
	for(unsigned int i = 0; i < notify_list.size(); i++) {
		if (notify_list[i].icon)
			notify_map[notify_spriteinfo.hpitch*i+0] = notify_list[i].icon->def_map;
		for(unsigned j = 0; j < notify_list[i].message.size(); j++) {
			notify_map[notify_spriteinfo.hpitch*i+j+1] =
				font_get_tile(notify_list[i].message[j],15,1);
		}
	}
}

static void Notify_Resize() {
	if (notify_spriteinfo.map_addr != 65535) {
		sprite_free_vmem(notify_spriteinfo.map_addr);
		notify_spriteinfo.map_addr = 65535;
	}
	if (notify_list.empty())  {
		notify_sprite.setVisible(false);
		return;
	}
	//Find the width. height is trivial.
	unsigned int width = 0;
	for(auto const &n : notify_list) {
		if (width < n.message.size())
			width = n.message.size();
	}
	//Allocate storage for the map
	notify_spriteinfo.hpitch = width+1;
	notify_spriteinfo.vsize = notify_list.size();
	notify_spriteinfo.hsize = width+1;
	notify_spriteinfo.hpos = ui::screen.rect().x + ui::screen.rect().width-notify_spriteinfo.hsize*8;
	notify_spriteinfo.vpos = ui::screen.rect().y + ui::screen.rect().height-notify_spriteinfo.vsize*8-24;
	unsigned addr = sprite_alloc_vmem(notify_spriteinfo.hpitch*notify_spriteinfo.vsize,
					  1, ~0U);
	if (addr != ~0U) {
		notify_spriteinfo.map_addr = addr;
		notify_map.resize(notify_spriteinfo.hpitch*notify_spriteinfo.vsize);
		Notify_Redraw();
		Notify_updatedMap();
		notify_sprite.setSpriteInfo(notify_spriteinfo);
		notify_sprite.setVisible(true);
	} else {
		notify_map.clear();
	}
}

static void Notify_UpdateNotifications() {
	//so, notifications changed.
	Notify_Resize();
	notify_updateQueued = false;
}

static void Notify_Timeout(void *) {
	//we just remove one element.
	ISR_Guard g;
	if (notify_list.front().icon)
		delete notify_list.front().icon;
	notify_list.pop_front();
	Notify_Resize();
}

void ui::Notification_Add(std::string const & message, Icon *icon) {
	ISR_Guard g;
	Notification n;
	n.icon = icon;
	n.message = message;
	notify_list.push_back(n);
	if (!notify_updateQueued) {
		notify_updateQueued = true;
		addDeferredWork(sigc::ptr_fun(Notify_UpdateNotifications));
	}
	Timer_Oneshot(5000000, &Notify_Timeout, NULL);
}
