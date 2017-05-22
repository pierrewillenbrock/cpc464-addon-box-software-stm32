
#pragma once

#include "refcounted.hpp"
#include <stdint.h>
#include <vector>
#include "bits.h"

/** \brief Functionality for handling CPC style disk images
 */
namespace dsk {

	class DiskSector;

	struct DiskFindSectorCommand {
		unsigned pcn;
		unsigned phn;
		unsigned C;
		unsigned H;
		unsigned R;
		unsigned N;
		bool mfm;
		bool deleted;
		bool find_any;
		void (*completion)(RefPtr<DiskSector> sector,
				   DiskFindSectorCommand *command);
	};

	class DiskSector : public Refcounted<DiskSector> {
	public:
		unsigned C;
		unsigned H;
		unsigned R;
		unsigned N;
		unsigned ST1;
		unsigned ST2;
		void *data;
		unsigned size;
		unsigned pcn;
		unsigned phn;
		virtual ~DiskSector() {}
	};

	class Disk : public Refcounted<Disk> {
	protected:
		int fd;
		unsigned char NumTracks;
		unsigned char NumSides;
		std::vector<uint32_t> TrackOffsetTable;
		enum {
			IDLE,
			PRELOAD,
			FIND
		} state;
		unsigned preload_cylinderno;
		unsigned current_cylinderno;
		std::vector<uint8_t> current_cylinder;
		DiskFindSectorCommand *current_command;
		unsigned current_sector;
		struct PReadInfo {
			Disk *_this;
			PReadCommand cmd;
		} preadinfo;
	public:
		bool write_protected;
		bool two_sided;
		int side_offset;
		virtual ~Disk() {}
		virtual void close() = 0;
		virtual void preloadCylinder(unsigned pcn) = 0;
		virtual void findSector(DiskFindSectorCommand *command) = 0;
	};

	RefPtr<Disk> openImage(char const *filename);
}
