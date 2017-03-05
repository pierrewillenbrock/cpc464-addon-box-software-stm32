
#include <fs/fat.h>

#include <deque>
#include <vector>
#include <string.h>
#include <block/msd.h>
#include <fs/vfs.hpp>
#include <lang.hpp>

struct FatDirInode;

struct FAT_Partition_priv {
	uint32_t first_block;
	uint32_t num_blocks;
	MSD_Info *msd;
	uint32_t blockno;
	std::vector<uint8_t> block;
	MSDReadCommand read_command;

	uint32_t fat_start_block;//relative to first_block
	uint32_t fat_block_count;
	uint32_t fat_count;
	uint64_t fs_num_blocks;
	uint32_t bytes_per_cluster;
	uint32_t blocks_per_cluster;
	uint32_t cluster_0_block;//relative to first_block, also does not
	                       //actually pointer to a valid cluster.
	RefPtr<Inode> rootInode;
	//need to store where the fat and its copies are kept
	//need to store where any other global info is kept
	enum { Fat12, Fat16, Fat32 } fattype;
};

static std::deque<FAT_Partition_priv*> partitions;

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

union FatDirEntry {
	struct {
		uint8_t state;//if 0, there are no more entries in this directory. if 0xe5, it is unused.
		uint8_t reserved1[10];
		uint8_t attributes;//if 0x0f, it is a long file name entry
		uint8_t reserved2[20];
	} __attribute__((packed)) detect;
	struct {
		char filename[8];
		char extension[3];
		uint8_t attributes;
		uint8_t reserved;
		uint8_t creation_time_10ms;
		uint16_t creation_2s:5;
		uint16_t creation_minute:6;
		uint16_t creation_h:5;
		uint16_t creation_day:5;
		uint16_t creation_mon:4;
		uint16_t creation_year:7;
		uint16_t access_day:5;
		uint16_t access_mon:4;
		uint16_t access_year:7;
		uint16_t high_cluster;
		uint16_t modification_2s:5;
		uint16_t modification_minute:6;
		uint16_t modification_h:5;
		uint16_t modification_day:5;
		uint16_t modification_mon:4;
		uint16_t modification_year:7;
		uint16_t low_cluster;
		uint32_t size;
	} __attribute__((packed)) regular;
	struct {
		uint8_t order;//bit 0x40 if last part of name, rest is block number starting at 1
		uint16_t name1[5];
		uint8_t attributes;
		uint8_t type;
		uint8_t checksum;
		uint16_t name2[6];
		uint16_t cluster;//always zero
		uint16_t name3[2];
	} __attribute__((packed)) longfilename;
} __attribute__((packed));


static void fetchBlock_cmpl(int res, MSDReadCommand *command) {
	FAT_Partition_priv *p = container_of(command, FAT_Partition_priv,
					     read_command);
	if (res != 0) {
		p->blockno = ~0U;
		return; //hmm. okay, well.
	}
	p->blockno = p->read_command.start_block - p->first_block;
}

static void fetchBlock(FAT_Partition_priv *priv, uint32_t block) {
	volatile uint32_t* blocknop = &priv->blockno;
	if (priv->blockno == block)
		return;
	priv->blockno = ~0U;

	priv->read_command.start_block = priv->first_block + block;
	priv->read_command.num_blocks = 1;
	priv->read_command.dst = priv->block.data();
	priv->read_command.completion = fetchBlock_cmpl;
	priv->msd->readBlocks(priv->msd->data, &priv->read_command);
	while(*blocknop == ~0U) {
		__WFI();
	}
}

/* caller fills: read_command->num_blocks, dst, completion.
*/
/* pre-condition: none
   post-condition: read_command->completion gets called once and only once
 */
static void fetchBlock_nb(FAT_Partition_priv *priv, uint32_t block,
			  MSDReadCommand *read_command) {
	read_command->start_block = priv->first_block + block;
	priv->msd->readBlocks(priv->msd->data, read_command);
}

static uint32_t findNextCluster(FAT_Partition_priv *priv, uint32_t cluster) {
	fetchBlock(priv, cluster*4/512 + priv->fat_start_block);
	return ((uint32_t*)priv->block.data())[cluster%(512/4)];
}

struct Fat_FindNextCluster_Command {
	FAT_Partition_priv *priv;
	uint32_t cluster;
	void (*completion)(uint32_t cluster,
			   Fat_FindNextCluster_Command *command);
	uint32_t blockno;//init with ~0U
	MSDReadCommand read_command;
	char buf[512];
};

/* pre-condition: none
   post-condition: command->completion gets called once and only once
 */
static void findNextCluster_nb_cmpl(int res, MSDReadCommand *command) {
	Fat_FindNextCluster_Command *p = container_of(
		command, Fat_FindNextCluster_Command, read_command);
	if (res != 0) {
		p->blockno = ~0U;
		p->completion(~0U, p);
	}
	p->blockno = command->start_block - p->priv->first_block;
	p->completion(
		((uint32_t*)p->buf)[p->cluster%(512/4)],
		p);
}

/* caller fills command->priv, cluster, completion. everything else gets used.
 */
/* pre-condition: none
   post-condition: command->completion gets called once and only once
 */
static void findNextCluster_nb(Fat_FindNextCluster_Command *command) {
	uint32_t blockno = command->cluster*4/512 +
		command->priv->fat_start_block;
	if (command->blockno == blockno) {
		command->completion(
			((uint32_t*)command->buf)[command->cluster%(512/4)],
			command);
		return;
	}
	command->read_command.num_blocks = 1;
	command->read_command.dst = command->buf;
	command->read_command.completion = findNextCluster_nb_cmpl;
	fetchBlock_nb(command->priv, blockno, &command->read_command);
}

struct FatInode;

struct FatInodeReadNb {
	PReadCommand *command;
	char *ptr;
	size_t len;
	off_t offset;
	int res;
	uint32_t current_cluster;
	uint32_t current_offset;
	RefPtr<FatInode> inode;
	Fat_FindNextCluster_Command findnextcluster_command;
};

struct FatInode : public Inode {
	FAT_Partition_priv *priv;
	uint32_t first_cluster;
	uint32_t size;
	uint32_t current_cluster;
	uint32_t current_offset;
	FatInode(FAT_Partition_priv *priv,
		 uint32_t first_cluster,
		 uint32_t size,
		 mode_t mode)
		: priv(priv)
		, first_cluster(first_cluster)
		, size(size)
		, current_cluster(first_cluster)
		, current_offset(0)
		{
			this->mode = mode;
		}
	virtual _ssize_t pread(void *ptr, size_t len, off_t offset);
	virtual _ssize_t pwrite(const void */*ptr*/, size_t /*len*/, off_t /*offset*/);
	void pread_nb_cmpl1(FatInodeReadNb *p);
	static void _pread_nb_cmpl1(uint32_t cluster,
				    Fat_FindNextCluster_Command *command);
	void pread_nb_cmpl2(int res, FatInodeReadNb *p);
	static void _pread_nb_cmpl2(int res, MSDReadCommand *command);
	virtual _ssize_t pread_nb(PReadCommand * command);
	virtual _ssize_t pwrite_nb(PWriteCommand * command);
};

struct Fat16RootDirInode : public Inode {
	FAT_Partition_priv *priv;
	uint32_t root_dir_start_block;//relative to first_block
	uint32_t root_dir_entry_count;
	Fat16RootDirInode(FAT_Partition_priv *priv) : priv(priv) {}
};

struct FatDirInode : public FatInode {
	FatDirInode(FAT_Partition_priv *priv,
		    uint32_t first_cluster,
		    uint32_t size,
		    mode_t mode)
		: FatInode(priv, first_cluster, size, mode)
		{}
	//first d_off is -1, -2 and -3 are reserved, rest is free for use.
	bool _readdir(off_t &d_off, std::string &name,
		      FatDirEntry &ent) {
		std::basic_string<uint16_t> long_name;
		while(1) {
			d_off++;
			int res = pread(&ent, sizeof(ent), d_off * sizeof(ent));
			if (res == 0 || res == -1)
				return false;

			if (ent.detect.state == 0)
				return false;
			if (ent.detect.state == 0xe5)
				continue;
			if (ent.detect.attributes == 0xf) {
				//the long file name entries are stored in
				//reverse order, i.E. the last one is first.
				//last entry marker
				if (ent.longfilename.order & 0x40)
					long_name.clear();
				uint16_t nm[13];
				unsigned i = 0;
				for(i = 0; i < 5; i++) {
					if (ent.longfilename.name1[i] == 0)
						break;
					nm[i+0] = ent.longfilename.name1[i];
				}
				if (i < 5) {
					long_name = std::basic_string<uint16_t>
						(nm,i+0) + long_name;
					continue;
				}
				for(i = 0; i < 6; i++) {
					if (ent.longfilename.name2[i] == 0)
						break;
					nm[i+5] = ent.longfilename.name2[i];
				}
				if (i < 6) {
					long_name = std::basic_string<uint16_t>
						(nm,i+5) + long_name;
					continue;
				}
				for(i = 0; i < 2; i++) {
					if (ent.longfilename.name3[i] == 0)
						break;
					nm[i+11] = ent.longfilename.name3[i];
				}
				long_name = std::basic_string<uint16_t>
					(nm,11+i) + long_name;
				continue;
			}
			if (ent.regular.attributes & 0x8) //volume label
				continue;

			//do we have a long name?
			if (!long_name.empty()) {
				name = Utf16ToUtf8(long_name);
			} else {
				name = "";
				for(unsigned i = 0; i < 8; i++) {
					if (ent.regular.filename[i] == ' ')
						break;
					name += ent.regular.filename[i];
				}
				name += ".";
				for(unsigned i = 0; i < 3; i++) {
					if (ent.regular.extension[i] == ' ')
						break;
					name += ent.regular.extension[i];
				}
			}
			return true;
		}

		return false;
	}
	virtual int lookup(RefPtr<Dentry> dent) {
		off_t d_off = -1;
		std::string name;
		FatDirEntry ent;
		while(_readdir(d_off, name, ent)) {
			if (name == dent->name) {
				mode = S_IRUSR | S_IRGRP | S_IROTH |
					S_IWUSR | S_IWGRP | S_IWOTH;
				if (ent.regular.attributes & 0x01)
					//readonly
					mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
				if (ent.regular.attributes & 0x02) {
					//hidden
					mode &= ~(S_IRWXO);
				}
				if (ent.regular.attributes & 0x04) {
					//system
					mode &= ~(S_IRWXG | S_IRWXO);
				}
				if (ent.regular.attributes & 0x20) {
					//archive
					mode &= ~S_IWOTH;
				}
				if (ent.regular.attributes & 0x10) {
					//that's a directory
					dent->inode = new FatDirInode(
						priv, ent.regular.low_cluster |
						(ent.regular.high_cluster << 16),
						ent.regular.size,
						mode | S_IFDIR |
						S_IXUSR | S_IXGRP | S_IXOTH);
				} else {
					//plain inode
					dent->inode = new FatInode(
						priv, ent.regular.low_cluster |
						(ent.regular.high_cluster << 16),
						ent.regular.size,
						mode | S_IFREG);
				}
				return 0;
			}
		}
		return 0;
	}
	//first d_off is -1, -2 and -3 are reserved, rest is free for use.
	virtual bool readdir(off_t &d_off, std::string &name) {
		FatDirEntry ent;
		return _readdir(d_off, name, ent);
	}
};

static void FAT_probe_cmpl(int res, MSDReadCommand *command);

static void FAT_probe_partition(uint32_t type, uint32_t first_block,
				uint32_t num_blocks, struct MSD_Info *msd) {
	if (type != 0x01 && type != 0x03 && type != 0x06 && type != 0x0b &&
	    type != 0x0c && type != 0x0e)
		return;
	//otherwise, create and register the filesystem support structure
	//and read the first sector of the partition
	FAT_Partition_priv *p = new FAT_Partition_priv();

	p->first_block = first_block;
	p->num_blocks = num_blocks;
	p->msd = msd;
	p->block.resize(512);
	partitions.push_back(p);
	p->read_command.start_block = first_block;
	p->read_command.num_blocks = 1;
	p->read_command.dst = p->block.data();
	p->read_command.completion = FAT_probe_cmpl;
	p->msd->readBlocks(p->msd->data, &p->read_command);
}

static void FAT_probe_cmpl(int res, MSDReadCommand *command) {
	FAT_Partition_priv *p = container_of(command, FAT_Partition_priv,
					     read_command);
	if (res != 0) {
		for(auto it = partitions.begin(); it != partitions.end();it++) {
			if (*it == p) {
				partitions.erase(it);
				delete p;
				break;
			}
		}
		return;
	}

	p->blockno = 0;
	FAT16_BootSector *fat16bs = (FAT16_BootSector*)p->block.data();
	FAT32_BootSector *fat32bs = (FAT32_BootSector*)p->block.data();
	if (memcmp(fat16bs->fatvariant,"FAT16",5) == 0 && 0) {
		p->fattype = FAT_Partition_priv::Fat12;
	} else if (memcmp(fat16bs->fatvariant,"FAT16",5) == 0 && 0) {
		p->fattype = FAT_Partition_priv::Fat16;
	} else if (memcmp(fat32bs->fatvariant,"FAT32",5) == 0) {
		p->fattype = FAT_Partition_priv::Fat32;
		p->fat_start_block = fat32bs->reserved_sectors*
			fat32bs->byte_per_sector/512;
		p->fat_block_count = fat32bs->sectors_per_fat*
			fat32bs->byte_per_sector/512;
		p->blocks_per_cluster = fat32bs->sectors_per_cluster*
			fat32bs->byte_per_sector/512;
		p->fat_count = fat32bs->fat_copies;
		p->fs_num_blocks = (uint64_t)fat32bs->num_sectors*
			fat32bs->byte_per_sector/512;
		p->cluster_0_block = p->fat_start_block +
			p->fat_block_count * p->fat_count -
			2 * p->blocks_per_cluster;
		p->bytes_per_cluster = p->blocks_per_cluster*512;

		p->rootInode = new FatDirInode(p, fat32bs->root_dir_cluster,
					       ~0U,
					       S_IFDIR |
					       S_IRWXU | S_IRWXG | S_IRWXO);
		VFS_RegisterFilesystem("fat",p->rootInode);
	} else {
		for(auto it = partitions.begin(); it != partitions.end();it++) {
			if (*it == p) {
				partitions.erase(it);
				delete p;
				break;
			}
		}
		return;
	}
}

static void FAT_remove_msd(struct MSD_Info *msd) {
	for(auto it = partitions.begin(); it != partitions.end();) {
		if ((*it)->msd == msd) {
			FAT_Partition_priv *p = *it;
			it = partitions.erase(it);
			VFS_UnregisterFilesystem(p->rootInode);
			delete p;
		} else
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

_ssize_t FatInode::pread(void *ptr, size_t len, off_t offset) {
	if ((unsigned)offset >= size)
		return 0;
	if (len + offset > size)
		len = size - offset;
	_ssize_t res = 0;
	char *cptr = (char*)ptr;
	if ((unsigned)offset < current_offset) {
		//need to restart from 0.
		current_offset = 0;
		current_cluster = first_cluster;
	}
	while(len > 0) {
		//scan fat to find the given cluster
		while ((unsigned)offset >= current_offset +
		       priv->bytes_per_cluster &&
		       current_cluster < 0xffffff7) {
			current_offset += priv->bytes_per_cluster;
			current_cluster = findNextCluster(priv, current_cluster);
		}
		if (current_cluster >= 0xffffff7)
			break;
		fetchBlock(priv,priv->cluster_0_block +
			   current_cluster * priv->blocks_per_cluster +
			   (offset - current_offset)/512);
		size_t l2 = 512 - (offset - current_offset) % 512;
		if(l2 > len)
			l2 = len;
		memcpy(cptr, priv->block.data() +
		       (offset - current_offset) % 512,
		       l2);
		len -= l2;
		offset += l2;
		res += l2;
		cptr += l2;
	}

	return res;
}
_ssize_t FatInode::pwrite(const void */*ptr*/, size_t /*len*/, off_t /*offset*/) {
	errno = EINVAL;
	return -1;
}

/* pre-condition: p is allocated using new.
   post-condition: p is deallocated, command->completion has been called.
*/
void FatInode::pread_nb_cmpl2(int res, FatInodeReadNb *p) {
	PReadCommand * command = p->command;
	if (res != 0) {
		{
			ISR_Guard g;
			current_cluster = p->current_cluster;
			current_offset = p->current_offset;
		}
		delete p;
		command->completion(-1,EIO,command);
		return;
	}
	p->findnextcluster_command.blockno =
		p->findnextcluster_command.read_command.start_block -
		priv->first_block;
	size_t l2 = 512 - (p->offset - p->current_offset) % 512;
	if(l2 > p->len)
		l2 = p->len;
	memcpy(p->ptr, p->findnextcluster_command.buf +
	       (p->offset - p->current_offset) % 512,
	       l2);
	p->len -= l2;
	p->offset += l2;
	p->res += l2;
	p->ptr += l2;
	pread_nb_cmpl1(p);
}

void FatInode::_pread_nb_cmpl2(int res, MSDReadCommand *command) {
	FatInodeReadNb *p = container_of(
		command, FatInodeReadNb,
		findnextcluster_command.read_command);
	p->inode->pread_nb_cmpl2(res, p);
}

/* pre-condition: p is allocated using new.
   post-condition: p is deallocated, command->completion has been called.
*/
void FatInode::pread_nb_cmpl1(FatInodeReadNb *p) {
	PReadCommand * command = p->command;
	if (p->current_cluster == ~0U) {
		p->current_offset = 0;
		p->current_cluster = first_cluster;
		{
			ISR_Guard g;
			current_cluster = p->current_cluster;
			current_offset = p->current_offset;
		}
		delete p;
		command->completion(-1,EIO,command);
		return;
	}

	while(p->len > 0) {
		//scan fat to find the given cluster
		if ((unsigned)p->offset >= p->current_offset +
		    priv->bytes_per_cluster &&
		    p->current_cluster < 0xffffff7) {
			p->current_offset += priv->bytes_per_cluster;
			p->findnextcluster_command.priv = priv;
			p->findnextcluster_command.cluster = p->current_cluster;
			p->findnextcluster_command.completion = _pread_nb_cmpl1;
			findNextCluster_nb(&p->findnextcluster_command);
			return;
		}
		if (p->current_cluster >= 0xffffff7)
			break;

		//found the cluster, now lets read it.
		uint32_t blockno = priv->cluster_0_block +
			p->current_cluster * priv->blocks_per_cluster +
			(p->offset - p->current_offset)/512;
		if (blockno != p->findnextcluster_command.blockno) {
			p->findnextcluster_command.read_command.num_blocks = 1;
			p->findnextcluster_command.read_command.dst =
				p->findnextcluster_command.buf;
			p->findnextcluster_command.read_command.completion = _pread_nb_cmpl2;
			fetchBlock_nb(priv, blockno, &p->findnextcluster_command.read_command);
			return;
		}
		size_t l2 = 512 - (p->offset - p->current_offset) % 512;
		if(l2 > p->len)
			l2 = p->len;
		memcpy(p->ptr, p->findnextcluster_command.buf +
		       (p->offset - p->current_offset) % 512,
		       l2);
		p->len -= l2;
		p->offset += l2;
		p->res += l2;
		p->ptr += l2;
	}
	//done.
	int res = p->res;
	{
		ISR_Guard g;
		current_cluster = p->current_cluster;
		current_offset = p->current_offset;
	}
	delete p;
	command->completion(res,0,command);
	return;
}

void FatInode::_pread_nb_cmpl1(uint32_t cluster,
			       Fat_FindNextCluster_Command *command) {
	FatInodeReadNb *p = container_of(command, FatInodeReadNb,
					 findnextcluster_command);
	p->current_cluster = cluster;
	p->inode->pread_nb_cmpl1(p);
}

/* pre-condition: none.
   post-condition: command->completion has been called.
*/
_ssize_t FatInode::pread_nb(PReadCommand * command) {
	if ((unsigned)command->offset >= size) {
		command->completion(0,0,command);
		return 0;
	}
	size_t len = command->len;
	if (len + command->offset > size)
		len = size - command->offset;
	if (len == 0) {
		command->completion(0,0,command);
		return 0;
	}
	FatInodeReadNb *p = new FatInodeReadNb();
	command->pdata = p;
	p->ptr = (char*)command->ptr;
	p->len = len;
	p->offset = command->offset;
	p->res = 0;
	p->inode = this;
	p->command = command;
	{
		ISR_Guard g;
		p->current_cluster = current_cluster;
		p->current_offset = current_offset;
	}
	//now, prepare to find the cluster. we cannot use the usual
	//infrastructure for that, it is not safe to use in interrupt.
	if ((unsigned)p->offset < p->current_offset) {
		//need to restart from 0.
		p->current_offset = 0;
		p->current_cluster = first_cluster;
	}
	pread_nb_cmpl1(p);
	return 0;
}

_ssize_t FatInode::pwrite_nb(PWriteCommand * command) {
	errno = EINVAL;
	return -1;
}
