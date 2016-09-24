
#include "msd.h"
#include <vector>
#include <deque>
#include <stdlib.h>

struct FS_priv {
	Filesystem_Info *info;
};

struct MSD_priv {
	MSD_Info *info;
	std::vector<uint8_t> mbr;
	MSD_priv() : info(NULL) {}
};

static std::deque<FS_priv> fss;
static std::deque<MSD_priv> msds;

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

static void MSD_MBR_read_cmpl(int res, void *data) {
	MSD_priv *p = (MSD_priv*)data;
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
				for(FS_priv & fp : fss) {
					fp.info->probe_partition(
						msdos->partitions[i].type,
						msdos->partitions[i].first_lba,
						msdos->partitions[i].num_blocks,
						p->info);
				}
			}
		}
	}
	p->mbr.resize(0);
}

void MSD_Register(struct MSD_Info *info) {
	msds.push_back(MSD_priv());
	msds.back().info = info;
	msds.back().mbr.resize(512);
	msds.back().info->readBlocks(info->data, 0, msds.back().mbr.data(), 1,
				     MSD_MBR_read_cmpl, &msds.back());
}

void MSD_Unregister(struct MSD_Info *info) {
	for(FS_priv & fp : fss)
		fp.info->remove_msd(info);
	for(auto it = msds.begin(); it != msds.end(); it++) {
		if (it->info == info) {
			msds.erase(it);
			break;
		}
	}
}

void FS_Register(struct Filesystem_Info *info) {
	FS_priv p;
	p.info = info;
	fss.push_back(p);
}
