
#include <mouse.hpp>

#include <input/input.hpp>
#include <fpga/sprite.hpp>
#include <fpga/layout.h>
#include <fpga/fpga_comm.h>
#include <ui/ui.hpp>
#include <bits.h>
#include <algorithm>

static int const mouse_scale = 2;

class MouseSprite {
public:
	uint32_t x;
	uint32_t y;
	int dx;
	int dy;
	Sprite sprite;
	sprite_info spriteinfo;
	uint8_t sprite_tile_base;
	uint16_t sprite_map_base;
	uint8_t buttons;
	void mouseEvent(MouseEvent const &ev);
	MouseSprite();
};

class MouseInput : public input::Listener, public Mouse {
public:
	bool exclusive;
	MouseEvent state;
	input::Device *dev;
	virtual void remove(input::Device */*dev*/);
	virtual void inputReport(input::Report const &rep);
	virtual std::string getName();
	virtual void setExclusive(bool exclusive);
	MouseInput(input::Device *dev);
};

class MouseDevInput : public input::DeviceListener {
public:
	virtual void inputDeviceAdd(input::Device *dev);
};

MouseSprite::MouseSprite()
	: x(ui::screen.rect().width *mouse_scale/2)
	, y(ui::screen.rect().height *mouse_scale/2)
	, buttons(0)
 {
}

void MouseSprite::mouseEvent(MouseEvent const &ev) {
	if (buttons != ev.buttons) {
		for(unsigned i = 0; i < 7; i++) {
			if ((~buttons & ev.buttons) & (1 << i)) {
				UI_mouseDown(i);
			}
			if ((buttons & ~ev.buttons) & (1 << i)) {
				UI_mouseUp(i);
			}
		}
		buttons = ev.buttons;
	}
	if (ev.delta_axis[0] != 0 || ev.delta_axis[1] != 0) {
		int32_t nx = x + ev.delta_axis[0];
		int32_t ny = y + ev.delta_axis[1];
		if (nx < 0)
			nx = 0;
		if (nx > ui::screen.rect().width*mouse_scale)
			nx = ui::screen.rect().width*mouse_scale;
		if (ny < 0)
			ny = 0;
		if (ny > ui::screen.rect().height*mouse_scale)
			ny = ui::screen.rect().height*mouse_scale;
		x = nx;
		y = ny;
		dx += ev.delta_axis[0];
		dy += ev.delta_axis[1];
		int rdx = 0;
		int rdy = 0;
		if (dx >= mouse_scale) {
			rdx++;
			dx -= mouse_scale;
		}
		if (dx <= -mouse_scale) {
			rdx--;
			dx += mouse_scale;
		}
		if (dy >= mouse_scale) {
			rdy++;
			dy -= mouse_scale;
		}
		if (dy <= -mouse_scale) {
			rdy--;
			dy += mouse_scale;
		}
		UI_mouseMove(ui::screen.rect().x + x/mouse_scale,
			     ui::screen.rect().y + y/mouse_scale, rdx, rdy);

		spriteinfo.hpos = x/mouse_scale+ui::screen.rect().x;
		spriteinfo.vpos = y/mouse_scale+ui::screen.rect().y;
		sprite.setSpriteInfo(spriteinfo);
		sprite.setVisible(true);
	}
	if (ev.delta_axis[2] != 0) {
		UI_mouseWheel(ev.delta_axis[2]);
	}
}

static MouseSprite mousesprite;

MouseInput::MouseInput(input::Device *dev) : dev(dev) {
	state.buttons = 0;
	state.delta_axis[0] = 0;
	state.delta_axis[1] = 0;
}

void MouseInput::inputReport( input::Report const &rep) {
	state.delta_axis[0] = 0;
	state.delta_axis[1] = 0;
	state.delta_axis[2] = 0;
	if ((rep.flags & input::Report::Relative) && rep.usage == 0x10030) {
		// X axis motion.
		state.delta_axis[0] = rep.value;
		if (!exclusive)
			mousesprite.mouseEvent(state);
	}
	if ((rep.flags & input::Report::Relative) && rep.usage == 0x10031) {
		// Y axis motion.
		state.delta_axis[1] = rep.value;
		if (!exclusive)
			mousesprite.mouseEvent(state);
	}
	if ((rep.flags & input::Report::Relative) && rep.usage == 0x10038) {
		// wheel
		state.delta_axis[2] = rep.value;
		if (!exclusive)
			mousesprite.mouseEvent(state);
	}
	if ((rep.usage & 0xffff0000) == 0x90000) {
		int btn = rep.usage & 0xffff;
		if (btn >= 1 && btn <= 8) {
			if (rep.value)
				state.buttons |= (1 << (btn-1));
			else
				state.buttons &= ~(1 << (btn-1));
		}
		if (!exclusive)
			mousesprite.mouseEvent(state);
	}
}

std::string MouseInput::getName() {
	return dev->name();
}

void MouseInput::setExclusive(bool exclusive) {
	if(this->exclusive == exclusive)
		return;
	if(exclusive) {
		if(state.buttons) {
			MouseEvent ev;
			ev.buttons = 0;
			ev.delta_axis[0] = 0;
			ev.delta_axis[1] = 0;
			ev.delta_axis[2] = 0;
			mousesprite.mouseEvent(ev);
		}
	} else {
		state.delta_axis[0] = 0;
		state.delta_axis[1] = 0;
		state.delta_axis[2] = 0;
		mousesprite.mouseEvent(state);
	}
	this->exclusive = exclusive;
}

static std::vector<RefPtr<MouseInput> > mouseinputs;
static MouseDevInput mousedevinput;

void MouseInput::remove(input::Device */*dev*/) {
	ISR_Guard g;
	auto it = std::find(mouseinputs.begin(), mouseinputs.end(), this);
	if (it == mouseinputs.end())
		return;
	mouseinputs.erase(it);
}

void MouseDevInput::inputDeviceAdd(input::Device *dev) {
	bool have_rel_x = false;
	bool have_rel_y = false;
	bool have_btn_1 = false;
	bool have_btn_2 = false;
	auto info = dev->getCurrentInputReports();
	for(auto const &rep : info) {
		if((rep.flags & input::Report::Relative) && rep.usage == 0x10030)
			have_rel_x = true;
		if((rep.flags & input::Report::Relative) && rep.usage == 0x10031)
			have_rel_y = true;
		if(rep.usage == 0x90001)
			have_btn_1 = true;
		if(rep.usage == 0x90002)
			have_btn_2 = true;
	}
	if (have_rel_x && have_rel_y && have_btn_1 && have_btn_2) {
		MouseInput *m = new MouseInput(dev);
		for(auto const &rep : info)
			m->inputReport(rep);
		dev->addListener(m);
		ISR_Guard g;
		mouseinputs.push_back(m);
	}
}

void Mouse_Setup() {
#define TILE_LINE(c1, c2, c3, c4, c5, c6, c7, c8, p12, p34, p56, p78)	\
	(((p12) << 8) |  ((c2) << 4) | (c1) | 				\
	 ((p34) << 24) |  ((c4) << 20) | ((c3) << 16)),			\
	(((p56) << 8) |  ((c6) << 4) | (c5) |				\
	 ((p78) << 24) |  ((c8) << 20) | ((c7) << 16))
	static uint32_t const tiles[] = {
		//top
		TILE_LINE(1,0,0,0,0,0,0,0, 0,0,0,0),
		TILE_LINE(1,1,0,0,0,0,0,0, 0,0,0,0),
		TILE_LINE(1,3,1,0,0,0,0,0, 0,0,0,0),
		TILE_LINE(1,3,3,1,0,0,0,0, 0,0,0,0),
		TILE_LINE(1,3,3,3,1,0,0,0, 0,0,0,0),
		TILE_LINE(1,3,3,3,3,1,0,0, 0,0,0,0),
		TILE_LINE(1,3,3,3,3,3,1,0, 0,0,0,0),
		TILE_LINE(1,3,3,3,3,3,3,1, 0,0,0,0),
		//bottom
		TILE_LINE(1,3,3,3,3,1,1,0, 0,0,0,0),
		TILE_LINE(1,3,1,3,3,1,0,0, 0,0,0,0),
		TILE_LINE(1,1,0,1,3,3,1,0, 0,0,0,0),
		TILE_LINE(0,0,0,1,3,3,1,0, 0,0,0,0),
		TILE_LINE(0,0,0,0,1,1,0,0, 0,0,0,0),
		TILE_LINE(0,0,0,0,0,0,0,0, 0,0,0,0),
		TILE_LINE(0,0,0,0,0,0,0,0, 0,0,0,0),
		TILE_LINE(0,0,0,0,0,0,0,0, 0,0,0,0),
	};
#undef TILE_LINE
	input::registerDeviceListener(&mousedevinput);
	mousesprite.sprite_tile_base =
	        sprite_alloc_vmem(0x10*2, 0x10, ~0U) / 0x10;
	FPGAComm_CopyToFPGA(FPGA_GRPH_SPRITES_RAM +
	                    mousesprite.sprite_tile_base*0x10*4,
	                    tiles, sizeof(tiles));
	mousesprite.sprite_map_base = sprite_alloc_vmem(2, 1, ~0U);
	uint32_t map[] = {
		//operator + promotes to (int), even if all arguments are uint8_t
		uint32_t((mousesprite.sprite_tile_base + 0) << 2),
		uint32_t((mousesprite.sprite_tile_base + 1) << 2),
	};
	FPGAComm_CopyToFPGA(FPGA_GRPH_SPRITES_RAM +
	                    mousesprite.sprite_map_base*4,
	                    map, sizeof(map));
	mousesprite.spriteinfo.hpos = -16;
	mousesprite.spriteinfo.vpos = -16;
	mousesprite.spriteinfo.map_addr = mousesprite.sprite_map_base;
	mousesprite.spriteinfo.hsize = 1;
	mousesprite.spriteinfo.vsize = 2;
	mousesprite.spriteinfo.hpitch = 1;
	mousesprite.spriteinfo.doublesize = 0;
	mousesprite.sprite.setSpriteInfo(mousesprite.spriteinfo);
	mousesprite.sprite.setZOrder(65536);
	mousesprite.sprite.setPriority(65536);
}

std::vector<RefPtr<Mouse> > Mouse_get() {
	std::vector<RefPtr<Mouse> > res;
	for(auto &j : mouseinputs) {
		res.push_back(j);
	}
	return res;
}

// kate: indent-width 8; indent-mode cstyle; replace-tabs off;
