
#include "fat.h"

#include <deque>
#include <vector>
#include <string.h>
#include "msd.h"

struct FAT_Partition_priv {
	uint32_t first_block;
	uint32_t num_blocks;
	struct MSD_Info *msd;
	std::vector<uint8_t> block;
	enum { Fat12, Fat16, Fat32 } fattype;
};

static std::deque<FAT_Partition_priv> partitions;

struct FAT16_BootSector {
	uint8_t code1[3];
	char OEM[8];
	uint16_t byte_per_sector;
	uint8_t sectors_per_cluster;
	uint16_t reserved_sectors;//including boot sector
	uint8_t fat_copies;
	uint16_t root_dir_entry_count;
	uint16_t num_sectors_short;
	uint8_t media_descriptor;
	uint16_t sectors_per_fat;
	uint16_t sectors_per_track;
	uint16_t head_count;
	uint32_t hidden_sectors;//blocks between mbr and this one, i.E. first_block-1
	uint32_t num_sectors;
	uint8_t physical_drive_number;
	uint8_t reserved;
	uint8_t extended_boot_signature;
	uint32_t filesystem_id;
	char name[11];
	char fatvariant[8];
	char code2[448];
	uint16_t signature;
} __attribute__((packed));

struct FAT32_BootSector {
	uint8_t code1[3];
	char OEM[8];
	uint16_t byte_per_sector;
	uint8_t sectors_per_cluster;
	uint16_t reserved_sectors;//including boot sector
	uint8_t fat_copies;
	uint16_t root_dir_entry_count;
	uint16_t num_sectors_short;
	uint8_t media_descriptor;
	uint16_t sectors_per_fat_short;
	uint16_t sectors_per_track;
	uint16_t head_count;
	uint32_t hidden_sectors;//blocks between mbr and this one, i.E. first_block-1
	uint32_t num_sectors;
	uint32_t sectors_per_fat;
	uint16_t fat_flags;
	uint16_t fat_version;
	uint32_t root_dir_cluster;
	uint16_t fs_information_sector;
	uint16_t boot_sector_copy_sector;
	char reserved1[12];
	uint8_t physical_drive_number;
	uint8_t reserved2;
	uint8_t extended_boot_signature;
	uint32_t filesystem_id;
	char name[11];
	char fatvariant[8];
	char code2[420];
	uint16_t signature;
} __attribute__((packed));

static void FAT_probe_cmpl(int res, void *data);

static void FAT_probe_partition(uint32_t type, uint32_t first_block,
				uint32_t num_blocks, struct MSD_Info *msd) {
	if (type != 0x01 && type != 0x03 && type != 0x06 && type != 0x0b &&
	    type != 0x0c && type != 0x0e)
		return;
	//otherwise, create and register the filesystem support structure
	//and read the first sector of the partition
	partitions.push_back(FAT_Partition_priv());
	partitions.back().first_block = first_block;
	partitions.back().num_blocks = num_blocks;
	partitions.back().msd = msd;
	partitions.back().block.resize(512);
	partitions.back().msd->readBlocks(partitions.back().msd->data,
					  first_block,
					  partitions.back().block.data(), 1,
					  FAT_probe_cmpl, &partitions.back());
}

static void FAT_probe_cmpl(int res, void *data) {
	FAT_Partition_priv *p = (FAT_Partition_priv*)data;
	if (res != 0) {
		for(auto it = partitions.begin(); it != partitions.end();it++) {
			if (&*it == data) {
				partitions.erase(it);
				break;
			}
		}
		return;
	}

	FAT16_BootSector *fat16bs = (FAT16_BootSector*)p->block.data();
	FAT32_BootSector *fat32bs = (FAT32_BootSector*)p->block.data();
	if (memcmp(fat16bs->fatvariant,"FAT16",5) == 0 && 0) {
		p->fattype = FAT_Partition_priv::Fat12;
	} else if (memcmp(fat16bs->fatvariant,"FAT16",5) == 0 && 0) {
		p->fattype = FAT_Partition_priv::Fat16;
	} else if (memcmp(fat32bs->fatvariant,"FAT32",5) == 0) {
		p->fattype = FAT_Partition_priv::Fat32;

		//todo: either register with syscall interface or continue initializing.
	} else {
		for(auto it = partitions.begin(); it != partitions.end();it++) {
			if (&*it == data) {
				partitions.erase(it);
				break;
			}
		}
		return;
	}
}

static void FAT_remove_msd(struct MSD_Info *msd) {
	for(auto it = partitions.begin(); it != partitions.end();) {
		if (it->msd == msd)
			it = partitions.erase(it);
		else
			it++;
	}
}

static Filesystem_Info fat_info = {
  .probe_partition = FAT_probe_partition,
  .remove_msd = FAT_remove_msd
};

void FAT_Setup() {
  FS_Register(&fat_info);
}
