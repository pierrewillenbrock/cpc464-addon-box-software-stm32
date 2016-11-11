
#include <block/msd.h>
#include <vector>
#include <deque>
#include <stdlib.h>

struct FS_priv {
	Filesystem_Info *info;
};

struct MSD_priv {
	MSD_Info *info;
	std::vector<uint8_t> mbr;
	MSDReadCommand readCommand;
	MSD_priv() : info(NULL) {}
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

static void MSD_MBR_read_cmpl(int res, MSDReadCommand *command) {
	MSD_priv *p = container_of(command, MSD_priv, readCommand);
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
					fp->info->probe_partition(
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
	MSD_priv *p = new MSD_priv;
	p->info = info;
	p->mbr.resize(512);
	msds.push_back(p);
	p->readCommand.start_block = 0;
	p->readCommand.num_blocks = 1;
	p->readCommand.dst = p->mbr.data();
	p->readCommand.completion = MSD_MBR_read_cmpl;
	p->info->readBlocks(info->data, &p->readCommand);
}

void MSD_Unregister(struct MSD_Info *info) {
	for(FS_priv* & fp : fss)
		fp->info->remove_msd(info);
	for(auto it = msds.begin(); it != msds.end(); it++) {
		if ((*it)->info == info) {
			MSD_priv *p = *it;
			msds.erase(it);
			delete p;
			break;
		}
	}
}

void FS_Register(struct Filesystem_Info *info) {
	FS_priv *p = new FS_priv;
	p->info = info;
	fss.push_back(p);
}
