
#include <fpga/sprite.hpp>
#include <fpga/fpga_comm.h>
#include <fpga/layout.h>
#include <fpga/fpga_uploader.hpp>
#include <irq.h>
#include <array>
#include <list>
#include <algorithm>

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

enum class BlockType : uint8_t {
	Invalid = 0,
	Unused = 1,
	Used = 2
};

struct BlockInfo {
	uint16_t size;
	uint8_t next;
	BlockType type; //0: does not exist, other data invalid. 1: unused. 2: used.
};

static std::array<BlockInfo,64> blocks = {{
		(BlockInfo) {0x780, 0xff, BlockType::Unused},
		(BlockInfo) {0, 0, BlockType::Invalid}
	}};

//if addr == ~0U, looks for space anywhere in the vmem
//return ~0U if it could not find room.
unsigned sprite_alloc_vmem(size_t size, unsigned align, unsigned addr) {
	ISR_Guard g;
	uint8_t p = 0;
	uint16_t ca = 0;
	if (addr != ~0U) {
		//find the block with the starting address
		while(p < blocks.size() && blocks[p].type != BlockType::Invalid) {
			if (ca <= addr && ca + blocks[p].size > addr)
				break;
			ca += blocks[p].size;
			p = blocks[p].next;
		}
		if (p >= blocks.size() || blocks[p].type != BlockType::Unused ||
			ca + blocks[p].size < addr + size)
			return ~0U;
	} else {
		uint8_t best_block = 0xff;
		while(p < blocks.size() && blocks[p].type != BlockType::Invalid) {
			uint16_t aa = ca;
			if (aa & (align-1)) {
				aa &= ~(align-1);
				aa += align;
			}
			if (blocks[p].size+ca > size+aa &&
			    blocks[p].type == BlockType::Unused) {
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
			return ~0U;
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
		blocks[p].type = BlockType::Used;
	} else if (ca == addr) {
		//aligns to the begin. need only one block entry
		unsigned int i;
		for(i = 0; i < blocks.size(); i++) {
			if (blocks[i].type == BlockType::Invalid)
				break;
		}
		if (i >= blocks.size())
			return ~0U;
		blocks[i].next = blocks[p].next;
		blocks[i].size = blocks[p].size-size;
		blocks[i].type = BlockType::Unused;
		blocks[p].type = BlockType::Used;
		blocks[p].size = size;
		blocks[p].next = i;
	} else if (ca+blocks[p].size == addr+size) {
		//aligns to the end. need only one block entry
		unsigned int i;
		for(i = 0; i < blocks.size(); i++) {
			if (blocks[i].type == BlockType::Invalid)
				break;
		}
		if (i >= blocks.size())
			return ~0U;
		blocks[i].next = blocks[p].next;
		blocks[i].size = size;
		blocks[i].type = BlockType::Used;
		blocks[p].type = BlockType::Unused;
		blocks[p].size = blocks[p].size-size;
		blocks[p].next = i;
	} else {
		//need two block entries.
		unsigned int i,j;
		for(i = 0; i < blocks.size(); i++) {
			if (blocks[i].type == BlockType::Invalid)
				break;
		}
		for(j = i+1; j < blocks.size(); j++) {
			if (blocks[j].type == BlockType::Invalid)
				break;
		}
		if (j >= blocks.size())
			return ~0U;
		blocks[j].type = BlockType::Unused;
		blocks[j].size = blocks[p].size - size - addr + ca;
		blocks[j].next = blocks[p].next;
		blocks[i].type = BlockType::Used;
		blocks[i].size = size;
		blocks[i].next = j;
		blocks[p].type = BlockType::Unused;
		blocks[p].size = addr - ca;
		blocks[p].next = i;
	}

	return addr;
}

void sprite_free_vmem(unsigned addr) {
	ISR_Guard g;
	uint8_t p = 0;
	uint8_t pp = 0xff;
	uint16_t ca = 0;
	//find the block with the starting address
	while(p < blocks.size() && blocks[p].type != BlockType::Invalid) {
		if (ca == addr)
			break;
		ca += blocks[p].size;
		pp = p;
		p = blocks[p].next;
	}
	assert(p < blocks.size() && blocks[p].type != BlockType::Invalid);
	if (p != 0 && blocks[pp].type == BlockType::Unused) {
		//merge into previous block
		blocks[pp].size += blocks[p].size;
		blocks[p].type = BlockType::Invalid;
		blocks[pp].next = blocks[p].next;
		p = pp;
	} else {
		blocks[p].type = BlockType::Unused;
	}
	uint8_t n = blocks[p].next;
	if(n < blocks.size() && blocks[n].type == BlockType::Unused) {
		//merge next block
		blocks[p].size += blocks[n].size;
		blocks[p].next = blocks[n].next;
		blocks[n].type = BlockType::Invalid;
	}
}

struct SpriteVMemInfo spritevmeminfo() {
	SpriteVMemInfo info;
	info.total = 0x780;
	info.largestFreeBlock = 0;
	info.free = 0;
	info.used = 0;

	ISR_Guard g;
	for(auto const &block : blocks) {
		if(block.type == BlockType::Unused) {
			info.free += block.size;
			if (block.size > info.largestFreeBlock)
				info.largestFreeBlock = block.size;
		}
		if(block.type == BlockType::Used)
			info.used += block.size;
	}

	return info;
}

static const sprite_info sprite_default = {
	.hpos = 65520,
	.vpos = 65520,
	.map_addr = 0,
	.hsize = 0,
	.vsize = 0,
	.hpitch = 0,
	.doublesize = 0,
	.reserved = 0
};
static std::list<Sprite *> sprite_registered;
static Sprite * sprite_allocated[4] = {0,0,0,0};
static FPGA_Uploader sprite_uploader[4];
static FPGA_Uploader sprite_map_uploader[4];

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
		// we already know bestprio[i] == NULL, so we skip that using i-1
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
	for(unsigned i = 0; i < 4; i++) {
		if(!sprite_allocated[i])
			continue;
		bool found = false;
		for(unsigned j = 0; j < 4; j++) {
			if (bestprio[j] == sprite_allocated[i]) {
				found = true;
				break;
			}
		}
		if(!found) {
			sprite_allocated[i]->m_allocated = -1;
			sprite_allocated[i]->freeMap(sprite_allocated[i]->m_info);
			sprite_allocated[i] = NULL;
			if (!bestprio[i]) {
				sprite_uploader[i].setSrc(&sprite_default);
				sprite_uploader[i].triggerUpload();
			}
		}
	}

	// all remaining entries in sprite_allocated are
	// * going to stay the same,
	// * get replaced by some other entry in sprite_allocated or
	// * get replaced by a new entry

	// update m_allocated in the bestprio sprites and triggerUpload and
	// allocateMap as needed.
	for (unsigned i = 0; i < 4; i++) {
		if(sprite_allocated[i] == bestprio[i] || !bestprio[i])
			continue; // nothing to do

		if(bestprio[i]->m_allocated ==  -1) {
			// has not been allocated before
			bestprio[i]->m_allocated = i;
			bestprio[i]->allocateMap(bestprio[i]->m_info);
		} else {
			// map should still be allocated, but we need to reupload info
			bestprio[i]->m_allocated = i;
		}
		bestprio[i]->triggerUpload();
		sprite_allocated[i] = bestprio[i];
	}
}

void Sprite::doRegister(Sprite *sprite) {
	ISR_Guard g;
	assert(std::find
	       (sprite_registered.begin(), sprite_registered.end(), sprite) ==
	       sprite_registered.end());;
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
	sprite->freeMap(sprite->m_info);
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

void Sprite::triggerMapUpload(uint32_t *data) {
	if(m_allocated < 0 || m_allocated >= 4 || m_info.map_addr == 65535)
		return;
	sprite_map_uploader[m_allocated].setSrc(data);
	sprite_map_uploader[m_allocated].setSize(m_info.hpitch*m_info.vsize * 4);
	sprite_map_uploader[m_allocated].setDest(
		FPGA_GRPH_SPRITES_RAM + m_info.map_addr*4);
	sprite_map_uploader[m_allocated].triggerUpload();
}

bool MappedSprite::allocateMap(sprite_info &i) {
	if (map_addr != 65535)
		freeMap(i);
	unsigned addr = sprite_alloc_vmem(i.hpitch*i.vsize,
					  1, ~0U);
	if (addr != ~0U) {
		map_addr = addr;
		i.map_addr = addr;
		triggerMapUpload(storage.data());
	} else {
		map_addr = 65535;
		i.map_addr = 65535;
	}
	return map_addr != 65535;
}

void MappedSprite::freeMap(sprite_info &i) {
	if (map_addr != 65535)
		sprite_free_vmem(map_addr);
	map_addr = 65535;
	i.map_addr = 65535;
}

MappedSprite::MappedSprite()
	: Sprite()
	, map_addr(65535) {
	sprite_info i = info();
	i.hpos = 65520;
	i.vpos = 65520;
	i.hsize = 0;
	i.hpitch = 0;
	i.vsize = 0;
	i.map_addr = 65535;
	setSpriteInfo(i);
}

MappedSprite::MappedSprite(MappedSprite const &sp)
	: Sprite()
	, storage(sp.storage)
	, map_addr(65535) {
	*this = sp;
}

MappedSprite &MappedSprite::operator=(MappedSprite const &sp) {
	sprite_info iorig = info();
	freeMap(iorig);
	setSpriteInfo(iorig);
	Sprite::operator=(sp);
	storage = sp.storage;
	sprite_info inew = info();
	if (isAllocated()) {
		allocateMap(inew);
		setSpriteInfo(inew);
		updateDone();
	}
	return *this;
}

MappedSprite::~MappedSprite() {
	sprite_info i = info();
	freeMap(i);
}

uint32_t const &MappedSprite::at(unsigned x, unsigned y) const {
	sprite_info const &i = info();
	return storage[i.hpitch * y + x];
}

uint32_t &MappedSprite::at(unsigned x, unsigned y) {
	sprite_info const &i = info();
	return storage[i.hpitch * y + x];
}

void MappedSprite::updateDone() {
	triggerMapUpload(storage.data());
}

void MappedSprite::setSize(unsigned x, unsigned y) {
	sprite_info i = info();
	i.hpitch = x;
	i.hsize = x;
	i.vsize = y;
	storage.resize(x*y);
	if (isAllocated()) {
		freeMap(i);
		allocateMap(i);
	}
	setSpriteInfo(i);
}

void MappedSprite::setPosition(unsigned x, unsigned y) {
	sprite_info i = info();
	i.hpos = x;
	i.vpos = y;
	setSpriteInfo(i);
}

void MappedSprite::setDoubleSize(bool doublesize) {
	sprite_info i = info();
	i.doublesize = doublesize ? 1 : 0;
	setSpriteInfo(i);
}
