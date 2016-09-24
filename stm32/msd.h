
#pragma once

#include <stdint.h>

struct MSD_Info {
  uint32_t size;
  uint32_t block_size;
  void *data;

  void (*readBlocks)(void *data, uint32_t block, void *dst, uint32_t num_blocks,
		     void (*cmpl)(int, void *), void *cmpl_data);
  void (*writeBlocks)(void *data, uint32_t block, void const *src,
		      uint32_t num_blocks,
		      void (*cmpl)(int, void *), void *cmpl_data);
};

struct Filesystem_Info {
  void (*probe_partition)(uint32_t type, uint32_t first_block,
			  uint32_t num_blocks, struct MSD_Info *msd);
  void (*remove_msd)(struct MSD_Info *msd);
};

#ifdef __cplusplus
extern "C" {
#endif

void MSD_Register(struct MSD_Info *info);
void MSD_Unregister(struct MSD_Info *info);
void FS_Register(struct Filesystem_Info *info);

#ifdef __cplusplus
}
#endif
