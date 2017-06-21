
#include <fdc/dsk.hpp>

#include <fcntl.h>
#include <vector>
#include <string.h>
#include <unistd.h>

namespace dsk {
	typedef struct
	{
		unsigned char	C;
		unsigned char	H;
		unsigned char	R;
		unsigned char	N;
		unsigned char	ST1;
		unsigned char	ST2;
		unsigned char	pad0;
		unsigned char	pad1;
	} DSKCHRN;

	typedef struct
	{
		char	TrackHeader[12];
		char	pad0[4];
		unsigned char	track;
		unsigned char	side;
		unsigned char	pad1[2];
		unsigned char	BPS;
		unsigned char	SPT;
		unsigned char	Gap3;
		unsigned char	FillerByte;
		DSKCHRN	SectorIDs[29];
	} DSKTRACKHEADER;

	typedef struct
	{
		char	     DskHeader[34];
		char	     DskCreator[14];
		unsigned char	NumTracks;
		unsigned char	NumSides;
		unsigned char	TrackSizeLow;
		unsigned char	TrackSizeHigh;
		char		pad0[255-4-14-33];
	} DSKHEADER;

	class DSK : public Disk {
	private:
		unsigned short TrackSize;
		void preloadReadComplete(int res, int errno_code);
		void fillSectorInfoAndComplete();
		void findSectorReadComplete(int res, int /*errno_code*/);
	public:
		bool probe(int fd);
		virtual void close();
		virtual void preloadCylinder(unsigned pcn);
		virtual void findSector(DiskFindSectorCommand *command);
	};

	typedef struct
	{
		unsigned char	C;
		unsigned char	H;
		unsigned char	R;
		unsigned char	N;
		unsigned char	ST1;
		unsigned char	ST2;
		unsigned char	SectorSizeLow;
		unsigned char	SectorSizeHigh;
	} EXTDSKCHRN;

	typedef struct
	{
		char	TrackHeader[12];
		char	pad0[4];
		unsigned char	track;
		unsigned char	side;
		unsigned char	pad1[2];
		unsigned char	BPS;
		unsigned char	SPT;
		unsigned char	Gap3;
		unsigned char	FillerByte;
		EXTDSKCHRN	SectorIDs[29];
	} EXTDSKTRACKHEADER;

	typedef struct
	{
		char		DskHeader[34];
		char		DskCreator[14];
		unsigned char	NumTracks;
		unsigned char	NumSides;
		unsigned char	TrackSizeLow;
		unsigned char	TrackSizeHigh;
		char		TrackSizeTable[255-4-14-33];
	} EXTDSKHEADER;

	class ExtDSK : public Disk {
	private:
		unsigned short TrackSize;
		std::vector<unsigned char> TrackSizeTable;
		void preloadReadComplete(int res, int errno_code);
		void fillSectorInfoAndComplete();
		void findSectorReadComplete(int res, int /*errno_code*/);
	public:
		bool probe(int fd);
		virtual void close();
		virtual void preloadCylinder(unsigned pcn);
		virtual void findSector(DiskFindSectorCommand *command);
	};
}

using namespace dsk;

RefPtr<Disk> dsk::openImage(char const *filename) {
	bool write_protected = false;
	int fd = open(filename, O_RDWR);
	if (fd == -1) {
		write_protected = true;
		fd = open(filename, O_RDONLY);
	}
	if (fd == -1) {
		return RefPtr<Disk>(NULL);
	}
	DSK *dsk = new DSK();
	if (dsk->probe(fd)) {
		dsk->write_protected = write_protected;
		return dsk;
	}
	delete dsk;
	ExtDSK *extdsk = new ExtDSK();
	if (extdsk->probe(fd)) {
		extdsk->write_protected = write_protected;
		return extdsk;
	}
	delete extdsk;
	return RefPtr<Disk>(NULL);
}

bool DSK::probe(int fd) {
	this->fd = fd;
	DSKHEADER h;
	lseek(fd, 0, SEEK_SET);
	read(fd, &h, sizeof(h));
	if (memcmp(h.DskHeader,"MV - CPC",8)!=0)
		return false;
	/* has main header */

	if ((h.NumSides!=1) && (h.NumSides!=2))
		return false;
	/* 1 or 2 sides */

	if (h.NumTracks<=1 || h.NumTracks>=85)
		return false;

	//extract info from header
	NumSides = h.NumSides;
	NumTracks = h.NumTracks;
	TrackSize = h.TrackSizeLow | (h.TrackSizeHigh << 8);
	two_sided = h.NumSides == 2;
	side_offset = 0;
	preload_cylinderno = ~0U;
	current_cylinderno = ~0U;
	state = IDLE;
	TrackOffsetTable.resize(NumSides * NumTracks);
	uint32_t offset = sizeof(DSKHEADER);
	for(unsigned int i = 0; i < NumSides*NumTracks; i++) {
		TrackOffsetTable[i] = offset;
		offset += TrackSize;
	}
	return true;
}

void DSK::close() {
	int f = fd;
	fd = -1;
	::close(f);
}

void DSK::preloadReadComplete(int res, int errno_code) {
	current_cylinderno = preload_cylinderno;
	if (res == -1)
		current_cylinderno = ~0U;
	if (state == FIND) {
		if (current_cylinderno == current_command->pcn) {
			findSectorReadComplete(res, errno_code);
		} else {
			unsigned TrackIndex = current_command->pcn * NumSides;
			unsigned CylinderSize = TrackSize * NumSides;
			current_cylinder.resize(CylinderSize);
			preadcmd.ptr = current_cylinder.data();
			preadcmd.len = CylinderSize;
			preadcmd.offset = TrackOffsetTable[TrackIndex];
			preadcmd.slot = sigc::mem_fun(this, &DSK::findSectorReadComplete);
			aio::pread(fd, &preadcmd);
		}
	} else {
		state = IDLE;
	}
}

void DSK::preloadCylinder(unsigned pcn) {
	if (pcn != current_cylinderno) {
		ISR_Guard g;
		preload_cylinderno = pcn;
		if (state == IDLE) {
			state = PRELOAD;
			unsigned TrackIndex = pcn * NumSides;
			unsigned CylinderSize = TrackSize * NumSides;
			current_cylinder.resize(CylinderSize);
			preadcmd.ptr = current_cylinder.data();
			preadcmd.len = CylinderSize;
			preadcmd.offset = TrackOffsetTable[TrackIndex];
			preadcmd.slot = sigc::mem_fun(this, &DSK::preloadReadComplete);
			aio::pread(fd, &preadcmd);
		}
	}
}

void DSK::fillSectorInfoAndComplete() {
	DiskFindSectorCommand *c = current_command;
	//init the sector info. pointer to read data can be provided already.
	//also check if the track header(s) is correct.
	unsigned phn = (c->phn + side_offset) % NumSides;
	unsigned offset = TrackSize * phn;
	DSKTRACKHEADER *h = reinterpret_cast<DSKTRACKHEADER *>
		(current_cylinder.data() + offset);

	RefPtr<DiskSector> d;
	unsigned indexctr = 0;
	char * sectorData = reinterpret_cast<char*>(h+1);
	for(unsigned i = 0; i < current_sector; i++) {
		sectorData += 128<<(h->SectorIDs[i].N&0x07);
	}
	while(indexctr < 2) {
		unsigned int i = current_sector;
		void *data = sectorData;
		sectorData += 128<<(h->SectorIDs[i].N&0x07);
		current_sector++;
		if (current_sector >= h->SPT) {
			indexctr++;
			current_sector = 0;
			sectorData = reinterpret_cast<char*>(h+1);
		}
		if ((h->SectorIDs[i].ST1 & 0x05) == 0) {
			if (c->find_any ||
			    (((c->deleted && (h->SectorIDs[i].ST2 &
					    0x80) != 0) ||
			      (!c->deleted && (h->SectorIDs[i].ST2 &
					       0x80) == 0)) &&
			     h->SectorIDs[i].C == c->C &&
			     h->SectorIDs[i].H == c->H &&
			     h->SectorIDs[i].R == c->R &&
			     h->SectorIDs[i].N == c->N)) {
				d = new DiskSector();
				d->C = h->SectorIDs[i].C;
				d->H = h->SectorIDs[i].H;
				d->R = h->SectorIDs[i].R;
				d->N = h->SectorIDs[i].N;
				d->ST1 = h->SectorIDs[i].ST1;
				d->ST2 = h->SectorIDs[i].ST2;
				d->pcn = c->pcn;
				d->phn = phn;
				d->data = data;
				d->size = 128<<(h->SectorIDs[i].N&0x07);
				break;
			}
		}
	}
	state = IDLE;
	current_command = NULL;
	c->slot(d);
}

void DSK::findSectorReadComplete(int res, int /*errno*/) {
	DiskFindSectorCommand *c = current_command;
	if (res == -1) {
		current_cylinderno = ~0U;
		state = IDLE;
		current_command = NULL;
		c->slot(RefPtr<DiskSector>());
		return;
	}
	current_cylinderno = c->pcn;
	//init the sector info. pointer to read data can be provided already.
	//also check if the track header(s) is correct.
	unsigned phn = (c->phn + side_offset) % NumSides;
	unsigned offset = TrackSize * phn;
	DSKTRACKHEADER *h = reinterpret_cast<DSKTRACKHEADER *>
		(current_cylinder.data() + offset);
	if (memcmp(h->TrackHeader,"Track-Info",10) != 0) {
		state = IDLE;
		current_cylinderno = ~0U;
		current_command = NULL;
		c->slot(RefPtr<DiskSector>());
		return;
	}
	fillSectorInfoAndComplete();
}


void DSK::findSector(DiskFindSectorCommand *command) {
	if (command->pcn > NumTracks || command->phn > NumSides) {
		command->slot(RefPtr<DiskSector>());
		return;
	}
	{
		ISR_Guard g;
		current_command = command;
	}
	if (command->pcn != current_cylinderno) {
		ISR_Guard g;
		if (state == IDLE) {
			state = FIND;
			unsigned TrackIndex = command->pcn * NumSides;
			unsigned CylinderSize = TrackSize * NumSides;
			current_cylinder.resize(CylinderSize);
			preadcmd.ptr = current_cylinder.data();
			preadcmd.len = CylinderSize;
			preadcmd.offset = TrackOffsetTable[TrackIndex];
			preadcmd.slot = sigc::mem_fun(this, &DSK::findSectorReadComplete);
			aio::pread(fd, &preadcmd);
			return;
		} else {
			state = FIND;
			//will be picked up once the current PRELOAD is done
			return;
		}
	}
	fillSectorInfoAndComplete();
}

bool ExtDSK::probe(int fd) {
	this->fd = fd;
	EXTDSKHEADER h;
	lseek(fd, 0, SEEK_SET);
	read(fd, &h, sizeof(h));
	if (memcmp(h.DskHeader,"EXTENDED",8)!=0)
		return false;
	/* has main header */

	if (h.NumSides!=1 && h.NumSides!=2)
		return false;
	/* single or double sided */

	if (h.NumTracks<=0 || h.NumTracks>=85)
		return false;
	/* between 1 and 84 tracks in image */

	//extract info from header
	NumSides = h.NumSides;
	NumTracks = h.NumTracks;
	TrackSize = h.TrackSizeLow | (h.TrackSizeHigh << 8);
	TrackSizeTable.resize(h.NumSides*h.NumTracks);
	memcpy(TrackSizeTable.data(), h.TrackSizeTable,
	       h.NumSides*h.NumTracks);
	two_sided = h.NumSides == 2;
	side_offset = 0;
	preload_cylinderno = ~0U;
	current_cylinderno = ~0U;
	state = IDLE;
	TrackOffsetTable.resize(NumSides * NumTracks);
	uint32_t offset = sizeof(DSKHEADER);
	for(unsigned int i = 0; i < NumSides*NumTracks; i++) {
		TrackOffsetTable[i] = offset;
		offset += TrackSizeTable[i] << 8;
	}
	return true;
}

void ExtDSK::close() {
	int f = fd;
	fd = -1;
	::close(f);
}

void ExtDSK::preloadReadComplete(int res, int errno_code) {
	current_cylinderno = preload_cylinderno;
	if (res == -1)
		current_cylinderno = ~0U;
	if (state == FIND) {
		if (current_cylinderno == current_command->pcn) {
			findSectorReadComplete(res, errno_code);
		} else {
			unsigned TrackIndex = current_command->pcn * NumSides;
			unsigned CylinderSize = 0;
			for(unsigned i = TrackIndex; i < TrackIndex + NumSides; i++) {
				if (i >= TrackSizeTable.size())
					break;
				CylinderSize += TrackSizeTable[i] << 8;
			}
			current_cylinder.resize(CylinderSize);
			preadcmd.ptr = current_cylinder.data();
			preadcmd.len = CylinderSize;
			preadcmd.offset = TrackOffsetTable[TrackIndex];
			preadcmd.slot = sigc::mem_fun(this, &ExtDSK::findSectorReadComplete);
			aio::pread(fd, &preadcmd);
		}
	} else {
		state = IDLE;
	}
}

void ExtDSK::preloadCylinder(unsigned pcn) {
	if (pcn != current_cylinderno) {
		ISR_Guard g;
		preload_cylinderno = pcn;
		if (state == IDLE) {
			state = PRELOAD;
			unsigned TrackIndex = pcn * NumSides;
			unsigned CylinderSize = 0;
			for(unsigned i = TrackIndex; i < TrackIndex + NumSides; i++) {
				if (i >= TrackSizeTable.size())
					break;
				CylinderSize += TrackSizeTable[i] << 8;
			}
			current_cylinder.resize(CylinderSize);
			preadcmd.ptr = current_cylinder.data();
			preadcmd.len = CylinderSize;
			preadcmd.offset = TrackOffsetTable[TrackIndex];
			preadcmd.slot = sigc::mem_fun(this, &ExtDSK::preloadReadComplete);
			aio::pread(fd, &preadcmd);
		}
	}
}

void ExtDSK::fillSectorInfoAndComplete() {
	DiskFindSectorCommand *c = current_command;
	//init the sector info. pointer to read data can be provided already.
	//also check if the track header(s) is correct.
	unsigned TrackIndex = c->pcn * NumSides;
	unsigned phn = (c->phn + side_offset) % NumSides;
	//does the track exist?
	if (TrackSizeTable[TrackIndex + phn] == 0) {
		state = IDLE;
		current_command = NULL;
		c->slot(RefPtr<DiskSector>());
		return;
	}
	unsigned offset = 0;
	for(unsigned i = 0; i < phn; i++)
		offset += TrackSizeTable[i] << 8;
	EXTDSKTRACKHEADER *h = reinterpret_cast<EXTDSKTRACKHEADER *>
		(current_cylinder.data() + offset);
	RefPtr<DiskSector> d;
	unsigned indexctr = 0;
	char * sectorData = reinterpret_cast<char*>(h+1);
	for(unsigned i = 0; i < current_sector; i++) {
		sectorData += (h->SectorIDs[i].SectorSizeHigh << 8) |
			h->SectorIDs[i].SectorSizeLow;
	}
	while(indexctr < 2) {
		unsigned int i = current_sector;
		void *data = sectorData;
		sectorData +=
			(h->SectorIDs[i].SectorSizeHigh << 8) |
			h->SectorIDs[i].SectorSizeLow;
		current_sector++;
		if (current_sector >= h->SPT) {
			indexctr++;
			current_sector = 0;
			sectorData = reinterpret_cast<char*>(h+1);
		}
		if ((h->SectorIDs[i].ST1 & 0x05) == 0) {
			if (c->find_any ||
			    (((c->deleted && (h->SectorIDs[i].ST2 &
					    0x80) != 0) ||
			      (!c->deleted && (h->SectorIDs[i].ST2 &
					       0x80) == 0)) &&
			     h->SectorIDs[i].C == c->C &&
			     h->SectorIDs[i].H == c->H &&
			     h->SectorIDs[i].R == c->R &&
			     h->SectorIDs[i].N == c->N)) {
				d = new DiskSector();
				d->C = h->SectorIDs[i].C;
				d->H = h->SectorIDs[i].H;
				d->R = h->SectorIDs[i].R;
				d->N = h->SectorIDs[i].N;
				d->ST1 = h->SectorIDs[i].ST1;
				d->ST2 = h->SectorIDs[i].ST2;
				d->pcn = c->pcn;
				d->phn = phn;
				d->data = data;
				d->size =
					(h->SectorIDs[i].SectorSizeHigh << 8) |
					h->SectorIDs[i].SectorSizeLow;
				break;
			}
		}
	}
	state = IDLE;
	current_command = NULL;
	c->slot(d);
}

void ExtDSK::findSectorReadComplete(int res, int /*errno*/) {
	DiskFindSectorCommand *c = current_command;
	if (res == -1) {
		current_cylinderno = ~0U;
		state = IDLE;
		current_command = NULL;
		c->slot(RefPtr<DiskSector>());
		return;
	}
	current_cylinderno = c->pcn;
	//init the sector info. pointer to read data can be provided already.
	//also check if the track header(s) is correct.
	unsigned TrackIndex = c->pcn * NumSides;
	unsigned phn = (c->phn + side_offset) % NumSides;
	//does the track exist?
	if (TrackSizeTable[TrackIndex + phn] == 0) {
		state = IDLE;
		current_command = NULL;
		c->slot(RefPtr<DiskSector>());
		return;
	}
	unsigned offset = 0;
	for(unsigned i = 0; i < phn; i++)
		offset += TrackSizeTable[i] << 8;
	EXTDSKTRACKHEADER *h = reinterpret_cast<EXTDSKTRACKHEADER *>
		(current_cylinder.data() + offset);
	if (memcmp(h->TrackHeader,"Track-Info",10) != 0) {
		state = IDLE;
		current_cylinderno = ~0U;
		current_command = NULL;
		c->slot(RefPtr<DiskSector>());
		return;
	}
	fillSectorInfoAndComplete();
}

void ExtDSK::findSector(DiskFindSectorCommand *command) {
	if (command->pcn > NumTracks || command->phn > NumSides) {
		command->slot(RefPtr<DiskSector>());
		return;
	}
	{
		ISR_Guard g;
		current_command = command;
	}
	if (command->pcn != current_cylinderno) {
		ISR_Guard g;
		if (state == IDLE) {
			state = FIND;
			unsigned TrackIndex = command->pcn * NumSides;
			unsigned CylinderSize = 0;
			for(unsigned i = TrackIndex; i < TrackIndex + NumSides; i++) {
				if (i >= TrackSizeTable.size())
					break;
				CylinderSize += TrackSizeTable[i] << 8;
			}
			current_cylinder.resize(CylinderSize);
			preadcmd.ptr = current_cylinder.data();
			preadcmd.len = CylinderSize;
			preadcmd.offset = TrackOffsetTable[TrackIndex];
			preadcmd.slot = sigc::mem_fun(this, &ExtDSK::findSectorReadComplete);
			aio::pread(fd, &preadcmd);
			return;
		} else {
			state = FIND;
			//will be picked up once the current PRELOAD is done
			return;
		}
	}
	fillSectorInfoAndComplete();
}

