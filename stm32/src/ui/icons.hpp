
#pragma once

#include <refcounted.hpp>
#include <fpga/fpga_comm.h>
#include <array>

namespace ui {
	struct Icon {
	private:
		void allocate() const;
		void deallocate() const;
		mutable unsigned refcount;
		friend class RefPtr<Icon const>;
		void refcountinc() const {
			if (!refcount)
				allocate();
			refcount++;
		}
		void refcountdec() const {
			refcount--;
			if (!refcount)
				deallocate();
		}
		void refcountnonzero() const { assert(refcount != 0); }
	public:
		Icon() : refcount(0) {}
		uint32_t def_map;
		uint32_t sel_map;
	};
	class Icons {
	private:
		FPGAComm_Command fpgacmd;
		uint8_t uploading;
		uint32_t tiledata[0x10];
		struct IconTile {
			uint16_t addr;
			uint8_t assigned[4];
			bool dirty;
			IconTile () : addr(0xffff), dirty(false) {
				for (auto &a : assigned) a = 0xff;
			}
		};
		std::array<Icon,3> m_icons;
		std::array<IconTile,1> m_icontiles;
		void allocateIcon(Icon const *icon);
		void deallocateIcon(Icon const *icon);
		void checkTiles();
		static void _fpgacmpl(int result, FPGAComm_Command *command);
		friend class Icon;
	public:
		Icons();
		enum IconName {
			Folder=0, File, NewFolder
		};
		RefPtr<Icon const> getIcon(IconName name);
	};

	extern Icons icons;
}
