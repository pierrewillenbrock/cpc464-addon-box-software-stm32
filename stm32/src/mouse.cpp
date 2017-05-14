
#include <mouse.h>

#include <input/input.hpp>
#include <fpga/sprite.hpp>
#include <fpga/layout.h>
#include <ui/ui.hpp>
#include <bits.h>

static int const mouse_scale = 2;

class MouseInput : public InputListener {
public:
	uint32_t x;
	uint32_t y;
	int dx;
	int dy;
	Sprite sprite;
	sprite_info spriteinfo;
	uint8_t sprite_tile_base;
	uint16_t sprite_map_base;
	virtual void inputReport(InputReport const &rep);
	   MouseInput()
		: x(ui::screen.rect().width*mouse_scale/2)
		, y(ui::screen.rect().height*mouse_scale/2)
		{}
};

class MouseDevInput : public InputDevListener {
public:
	virtual void inputDeviceAdd(InputDev *dev);
};



void MouseInput::inputReport(InputReport const &rep) {
	if ((rep.flags & 1) && rep.usage == 0x10030) {
		// X axis motion.
		int32_t nx = x + rep.value;
		if (nx < 0)
			nx = 0;
		if (nx > ui::screen.rect().width*mouse_scale)
			nx = ui::screen.rect().width*mouse_scale;
		x = nx;
		dx += rep.value;
		int rdx = 0;
		if (dx >= mouse_scale) {
			rdx++;
			dx -= mouse_scale;
		}
		if (dx <= -mouse_scale) {
			rdx--;
			dx += mouse_scale;
		}
		UI_mouseMove(ui::screen.rect().x + x/mouse_scale,
			     ui::screen.rect().y + y/mouse_scale, rdx, 0);
	}
	if ((rep.flags & 1) && rep.usage == 0x10031) {
		// Y axis motion.
		int32_t ny = y + rep.value;
		if (ny < 0)
			ny = 0;
		if (ny > ui::screen.rect().height*mouse_scale)
			ny = ui::screen.rect().height*mouse_scale;
		y = ny;
		dy += rep.value;
		int rdy = 0;
		if (dy >= mouse_scale) {
			rdy++;
			dy -= mouse_scale;
		}
		if (dy <= -mouse_scale) {
			rdy--;
			dy += mouse_scale;
		}
		UI_mouseMove(ui::screen.rect().x + x/mouse_scale,
			     ui::screen.rect().y + y/mouse_scale, 0, rdy);
	}
	if ((rep.flags & 1) && rep.usage == 0x10038) {
		// wheel
		UI_mouseWheel(rep.value);
	}
	if ((((rep.flags & 1) && rep.usage == 0x10030) ||
	     ((rep.flags & 1) && rep.usage == 0x10031))) {
		spriteinfo.hpos = x/mouse_scale+ui::screen.rect().x;
		spriteinfo.vpos = y/mouse_scale+ui::screen.rect().y;
		sprite.setSpriteInfo(spriteinfo);
		sprite.setVisible(true);
	}
	if (rep.usage == 0x90001) {
		//button #1 info. probably should filter for mouse-like devices.
		if (rep.value)
			UI_mouseDown(0);
		else
			UI_mouseUp(0);
	}
	if (rep.usage == 0x90002) {
		//button #2 info. probably should filter for mouse-like devices.
		if (rep.value)
			UI_mouseDown(1);
		else
			UI_mouseUp(1);
	}
	if (rep.usage == 0x90003) {
		//button #3 info. probably should filter for mouse-like devices.
		if (rep.value)
			UI_mouseDown(2);
		else
			UI_mouseUp(2);
	}
}

static MouseInput mouseinput;
static MouseDevInput mousedevinput;

void MouseDevInput::inputDeviceAdd(InputDev *dev) {
	dev->addListener(&mouseinput);
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
	   mouseinput.refIsStatic();
	Input_registerDeviceListener(&mousedevinput);
	   mouseinput.sprite_tile_base =
		sprite_alloc_vmem(0x10*2, 0x10, ~0U) / 0x10;
	FPGAComm_CopyToFPGA(FPGA_GRPH_SPRITES_RAM +
			                     mouseinput.sprite_tile_base*0x10*4,
			    tiles, sizeof(tiles));
	   mouseinput.sprite_map_base = sprite_alloc_vmem(2, 1, ~0U);
	uint32_t map[] = {
		//operator + promotes to (int), even if all arguments are uint8_t
		uint32_t((mouseinput.sprite_tile_base + 0) << 2),
		uint32_t((mouseinput.sprite_tile_base + 1) << 2),
	};
	FPGAComm_CopyToFPGA(FPGA_GRPH_SPRITES_RAM +
			    mouseinput.sprite_map_base*4,
			    map, sizeof(map));
	   mouseinput.spriteinfo.hpos = -16;
	   mouseinput.spriteinfo.vpos = -16;
	   mouseinput.spriteinfo.map_addr = mouseinput.sprite_map_base;
	   mouseinput.spriteinfo.hsize = 1;
	   mouseinput.spriteinfo.vsize = 2;
	   mouseinput.spriteinfo.hpitch = 1;
	   mouseinput.spriteinfo.doublesize = 0;
	   mouseinput.sprite.setSpriteInfo(mouseinput.spriteinfo);
	   mouseinput.sprite.setZOrder(65536);
	   mouseinput.sprite.setPriority(65536);
}
