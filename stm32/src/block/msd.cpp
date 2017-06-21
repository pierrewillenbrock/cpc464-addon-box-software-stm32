
#include <block/msd.hpp>
#include <vector>
#include <deque>
#include <stdlib.h>

struct FS_priv {
	FilesystemDriver *drv;
};

struct MSD_priv {
	MSD *msd;
	std::vector<uint8_t> mbr;
	MSDReadCommand readCommand;
	MSD_priv() : msd(NULL) {}
};

static std::deque<FS_priv*> fss;
static std::deque<MSD_priv*> msds;

struct MSD_MSDOS_MBR {
	char code[446];
	struct {
		uint8_t status;
		uint8_t first_chs[3];
		uint8_t type;
		uint8_t last_chs[3];
		uint32_t first_lba;
		uint32_t num_blocks;
	} __attribute__((packed)) partitions[4];
	uint16_t signature;
} __attribute__((packed));

static void MSD_MBR_read_cmpl(int res, MSD_priv *p) {
	if (res == 0) {
		MSD_MSDOS_MBR *msdos = (MSD_MSDOS_MBR*)p->mbr.data();
		if (msdos->signature == 0xaa55) {
			//looks like an mbr to me.
			for(unsigned i = 0; i < 4; i++) {
				if ((msdos->partitions[i].status != 0x80 &&
				     msdos->partitions[i].status != 0x00) ||
				    msdos->partitions[i].type == 0x00)
					continue;
				//valid.
				for(FS_priv* & fp : fss) {
					fp->drv->probe_partition(
						msdos->partitions[i].type,
						msdos->partitions[i].first_lba,
						msdos->partitions[i].num_blocks,
						p->msd);
				}
			}
		}
	}
	p->mbr.resize(0);
}

void MSD_Register(MSD *msd) {
	MSD_priv *p = new MSD_priv;
	p->msd = msd;
	p->mbr.resize(512);
	msds.push_back(p);
	p->readCommand.start_block = 0;
	p->readCommand.num_blocks = 1;
	p->readCommand.dst = p->mbr.data();
	p->readCommand.slot = sigc::bind(sigc::ptr_fun(&MSD_MBR_read_cmpl), p);
	p->msd->readBlocks(&p->readCommand);
}

void MSD_Unregister(MSD *msd) {
	for(FS_priv* & fp : fss)
		fp->drv->remove_msd(msd);
	for(auto it = msds.begin(); it != msds.end(); it++) {
		if ((*it)->msd == msd) {
			MSD_priv *p = *it;
			msds.erase(it);
			delete p;
			break;
		}
	}
}

void FSDriver_Register(FilesystemDriver *drv) {
	FS_priv *p = new FS_priv;
	p->drv = drv;
	fss.push_back(p);
}
