
#include <ui/notify.hpp>
#include <ui/ui.hpp>
#include <fpga/sprite.hpp>
#include <fpga/font.h>
#include <deque>
#include <deferredwork.hpp>
#include <timer.hpp>
#include <ui/icons.hpp>
#include "controls.hpp"

using namespace ui;

struct Notification {
	Icon *icon;
	std::string message;
};

static MappedSprite notify_sprite;
static std::deque<Notification> notify_list;
static bool notify_updateQueued = false;

void ui::Notification_Setup() {
	notify_sprite.setPriority(5);
	notify_sprite.setZOrder(40);
	notify_sprite.setDoubleSize(false);
}

static void Notify_Resize() {
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
	unsigned w = width+1;
	unsigned h = notify_list.size();
	notify_sprite.setPosition(ui::screen.rect().x + ui::screen.rect().width-w*8,
		ui::screen.rect().y + ui::screen.rect().height-h*8-24);
	notify_sprite.setSize(w,h);

	uint32_t empty_tile = font_get_tile(' ', palette.notify.sel,
	                                    palette.notify.idx);
	for(unsigned y = 0; y < h; y++)
		for(unsigned x = 0; x < w; x++)
			notify_sprite.at(x,y) = empty_tile;
	for(unsigned int i = 0; i < notify_list.size(); i++) {
		if (notify_list[i].icon)
			notify_sprite.at(0,i) = notify_list[i].icon->def_map;
		for(unsigned j = 0; j < notify_list[i].message.size(); j++) {
			notify_sprite.at(j+1,i) =
			        font_get_tile(notify_list[i].message[j],
			                      palette.notify.sel,
			                      palette.notify.idx);
		}
	}

	notify_sprite.updateDone();
	notify_sprite.setVisible(true);
}

static void Notify_UpdateNotifications() {
	//so, notifications changed.
	Notify_Resize();
	notify_updateQueued = false;
}

static void Notify_Timeout() {
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
	Timer_Oneshot(5000000, sigc::ptr_fun(&Notify_Timeout));
}
