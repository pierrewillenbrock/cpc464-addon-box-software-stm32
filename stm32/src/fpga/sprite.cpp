
#include <fpga/sprite.hpp>
#include <fpga/fpga_comm.h>
#include <fpga/layout.h>
#include <fpga/fpga_uploader.hpp>
#include <irq.h>
#include <array>
#include <list>

static uint16_t palette[128] = { 0 };

void sprite_set_palette(unsigned num, uint8_t const *data) {
	if (num >= 16)
		return;
	if (num < 8) {
		for(unsigned int i = 0; i < 16; i++) {
			palette[num*16+i] &= ~0x1f;
			palette[num*16+i] |= (*data++) & 0x1f;
		}
	} else {
		for(unsigned int i = 0; i < 16; i++) {
			palette[num*16-128+i] &= ~0x1e0;
			palette[num*16-128+i] |= (*data++ << 5) & 0x1e0;
		}
	}
}

static FPGA_Uploader sprite_palette_uploader;

void sprite_upload_palette() {
	sprite_palette_uploader.setDest(FPGA_GRPH_SPRITES_PALETTE);
	sprite_palette_uploader.setSrc(palette);
	sprite_palette_uploader.setSize(sizeof(palette));
	sprite_palette_uploader.triggerUpload();
}

struct BlockInfo {
	uint16_t size;
	uint8_t next;
	uint8_t type;//0: does not exist, other data invalid. 1: unused. 2: used.
};

static std::array<BlockInfo,64> blocks = { {(BlockInfo){ 0x780, 0xff, 1 }, (BlockInfo){ 0, 0, 0 }} };

//if addr == ~0U, looks for space anywhere in the vmem
//return ~0U if it could not find room.
unsigned sprite_alloc_vmem(size_t size, unsigned align, unsigned addr) {
	uint32_t isrlevel;
	ISR_Disable(isrlevel);
	uint8_t p = 0;
	uint16_t ca = 0;
	if (addr != ~0U) {
		//find the block with the starting address
		while(p < blocks.size() && blocks[p].type != 0) {
			if (ca <= addr && ca + blocks[p].size > addr)
				break;
			ca += blocks[p].size;
			p = blocks[p].next;
		}
		if (p >= blocks.size() || blocks[p].type != 1 ||
			ca + blocks[p].size < addr + size)
			goto fail;
	} else {
		uint8_t best_block = 0xff;
		while(p < blocks.size() && blocks[p].type != 0) {
			uint16_t aa = ca;
			if (aa & (align-1)) {
				aa &= ~(align-1);
				aa += align;
			}
			if (blocks[p].size+ca > size+aa &&
			    blocks[p].type == 1) {
				if (best_block >= blocks.size() ||
				    blocks[best_block].size > blocks[p].size) {
					best_block = p;
					addr = ca;
				}
			}
			ca += blocks[p].size;
			p = blocks[p].next;
		}
		if (best_block >= blocks.size())
			goto fail;
		p = best_block;
		ca = addr;
		if (addr & (align-1)) {
			addr &= ~(align-1);
			addr += align;
		}
	}
	//okay, got a block.
	//is it right size already?
	if (ca == addr && blocks[p].size == size) {
		//just mark it and be done.
		blocks[p].type = 2;
	} else if (ca == addr) {
		//aligns to the begin. need only one block entry
		unsigned int i;
		for(i = 0; i < blocks.size(); i++) {
			if (blocks[i].type == 0)
				break;
		}
		if (i >= blocks.size())
			goto fail;
		blocks[i].next = blocks[p].next;
		blocks[i].size = blocks[p].size-size;
		blocks[i].type = 1;
		blocks[p].type = 2;
		blocks[p].size = size;
		blocks[p].next = i;
	} else if (ca+blocks[p].size == addr+size) {
		//aligns to the end. need only one block entry
		unsigned int i;
		for(i = 0; i < blocks.size(); i++) {
			if (blocks[i].type == 0)
				break;
		}
		if (i >= blocks.size())
			goto fail;
		blocks[i].next = blocks[p].next;
		blocks[i].size = size;
		blocks[i].type = 2;
		blocks[p].type = 1;
		blocks[p].size = blocks[p].size-size;
		blocks[p].next = i;
	} else {
		//need two block entries.
		unsigned int i,j;
		for(i = 0; i < blocks.size(); i++) {
			if (blocks[i].type == 0)
				break;
		}
		for(j = i+1; j < blocks.size(); j++) {
			if (blocks[j].type == 0)
				break;
		}
		if (j >= blocks.size())
			goto fail;
		blocks[j].type = 1;
		blocks[j].size = blocks[p].size - size - addr + ca;
		blocks[j].next = blocks[p].next;
		blocks[i].type = 2;
		blocks[i].size = size;
		blocks[i].next = j;
		blocks[p].type = 1;
		blocks[p].size = addr - ca;
		blocks[p].next = i;
	}

	ISR_Enable(isrlevel);
	return addr;
fail:
	ISR_Enable(isrlevel);
	return ~0U;
}

void sprite_free_vmem(unsigned addr) {
	uint32_t isrlevel;
	ISR_Disable(isrlevel);
	uint8_t p = 0;
	uint8_t pp = 0xff;
	uint16_t ca = 0;
	//find the block with the starting address
	while(p < blocks.size() && blocks[p].type != 0) {
		if (ca == addr)
			break;
		ca += blocks[p].size;
		pp = p;
		p = blocks[p].next;
	}
	if (p >= blocks.size() || blocks[p].type == 0)
		goto out;
	if (p != 0 && blocks[pp].type == 1) {
		//merge into previous block
		blocks[pp].size += blocks[p].size;
		blocks[p].type = 0;
		blocks[pp].next = blocks[p].next;
		p = pp;
	}
	{
		uint8_t n = blocks[p].next;
		if (n < blocks.size() && blocks[n].type == 1) {
			//merge next block
			blocks[p].size += blocks[n].size;
			blocks[p].next = blocks[n].next;
			blocks[p].type = 1;
			blocks[n].type = 0;
		}
	}
out:
	ISR_Enable(isrlevel);
}

static const sprite_info sprite_default = {
	.hpos = 65520,
	.vpos = 65520,
	.map_addr = 0,
	.hsize = 0,
	.vsize = 0,
	.hpitch = 0,
	.doublesize = 0
};
static std::list<Sprite *> sprite_registered;
static Sprite * sprite_allocated[4] = {0,0,0,0};
static FPGA_Uploader sprite_uploader[4];

void Sprite_Setup() {
	for(unsigned i = 0; i < 4; i++) {
		sprite_uploader[i].setDest(FPGA_GRPH_SPRITE_BASE(i));
		sprite_uploader[i].setSrc(&sprite_default);
		sprite_uploader[i].setSize(sizeof sprite_default);
		sprite_uploader[i].triggerUpload();
	}
}

void Sprite::checkAllocations() {
	//find the four sprites with the highest priority
	ISR_Guard g;
	Sprite* bestprio[4] = {0,0,0,0};
	for(auto const &sp : sprite_registered) {
		unsigned replid = 0;
		for(unsigned i = 0; i < 4; i++) {
			if (!bestprio[i]) {
				replid = i;
				break;
			}
			if (bestprio[replid] &&
			    bestprio[i]->m_priority < bestprio[replid]->m_priority) {
				replid = i;
			}
		}
		if (!bestprio[replid] ||
		    bestprio[replid]->m_priority < sp->m_priority)
			bestprio[replid] = sp;
	}
	//order the four sprites by zorder
	for(unsigned i = 0; i < 3; i++) {
		for(unsigned j = i+1; j < 4; j++) {
			if (!bestprio[i] ||
			    (bestprio[j] &&
			     bestprio[i]->m_zorder < bestprio[j]->m_zorder)) {
				Sprite *t = bestprio[i];
				bestprio[i] = bestprio[j];
				bestprio[j] = t;
			}
		}
	}
	//try to shuffle unallocated space in bestprio so it matches m_allocated in
	//the sprites. the unallocated space is at the end, due to the sorting done
	//above.
	for(int i = 3; i >= 0; i--) {
		if (bestprio[i])
			break;
		for(int j = i-1; j >= 0; j--) {
			if (!bestprio[j])
				continue;
			if (bestprio[j]->m_allocated == i || bestprio[j]->m_allocated == -1) {
				bestprio[i] = bestprio[j];
				bestprio[j] = NULL;
			}
			break;
		}
	}
	//now fix up all sprites that lost their allocation
	for(auto const &sp : sprite_registered) {
		if (sp->m_allocated == -1)
			break;
		bool found = false;
		for(unsigned i = 0; i < 4; i++) {
			if (bestprio[i] == sp) {
				found = true;
				break;
			}
		}
		if (!found)
			sp->m_allocated = -1;
	}
	//and queue info updates for any changes in allocated sprite number
	for(unsigned i = 0; i < 4; i++) {
		if (!bestprio[i]) {
			if (sprite_allocated[i]) {
				sprite_allocated[i] = NULL;
				sprite_uploader[i].setSrc(&sprite_default);
				sprite_uploader[i].triggerUpload();
			}
			continue;
		}
		if (bestprio[i] != sprite_allocated[i]) {
			bestprio[i]->m_allocated = i;
			bestprio[i]->triggerUpload();
			sprite_allocated[i] = bestprio[i];
		}
	}
}

void Sprite::doRegister(Sprite *sprite) {
	ISR_Guard g;
	sprite_registered.push_back(sprite);
	checkAllocations();
}

void Sprite::unregister(Sprite *sprite) {
	ISR_Guard g;
	for(auto it = sprite_registered.begin(); it != sprite_registered.end(); it++) {
		if (*it == sprite) {
			sprite_registered.erase(it);
			checkAllocations();
			break;
		}
	}
}

Sprite::Sprite()
	: m_visible(false)
	, m_allocated(-1)
	, m_zorder(0)
	, m_priority(0)
{
}

Sprite::Sprite(Sprite const &sp)
	: m_visible(sp.m_visible)
	, m_allocated(-1)
	, m_zorder(sp.m_zorder)
	, m_priority(sp.m_priority)
	, m_info(sp.m_info)
{
	if (m_visible)
		doRegister(this);
}

Sprite &Sprite::operator=(Sprite const &sp) {
	if (m_visible && !sp.m_visible) {
		m_visible = false;
		unregister(this);
	}
	//we are not setting m_allocated here, since that is owned by the allocation
	//system.
	m_zorder = sp.m_zorder;
	m_priority = sp.m_priority;
	m_info = sp.m_info;
	if (!m_visible && sp.m_visible) {
		m_visible = true;
		doRegister(this);
	} else {
		m_visible = sp.m_visible;
		if (m_visible)
			checkAllocations();
	}
	return *this;
}

Sprite::~Sprite() {
	if (m_visible)
		unregister(this);
}

void Sprite::triggerUpload() {
	assert(m_allocated >= 0 && m_allocated < 4);
	sprite_uploader[m_allocated].setSrc(&m_info);
	sprite_uploader[m_allocated].triggerUpload();
}

void Sprite::setZOrder(unsigned zorder) {
	m_zorder = zorder;
	if (m_visible)
		checkAllocations();
}

void Sprite::setPriority(unsigned priority) {
	m_priority = priority;
	if (m_visible)
		checkAllocations();
}

void Sprite::setVisible(bool visible) {
	if (m_visible == visible)
		return;
	m_visible = visible;
	if (m_visible)
		doRegister(this);
	else
		unregister(this);
}

void Sprite::setSpriteInfo(struct sprite_info const &info) {
	m_info = info;
	if (isAllocated())
		triggerUpload();
}

bool Sprite::isAllocated() {
	return m_allocated != -1;
}


