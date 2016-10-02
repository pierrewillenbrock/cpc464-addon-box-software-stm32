
#pragma once

#include <stdint.h>
#include "sdio.h"

#ifdef __cplusplus
#define container_of(ptr, type, member) ({			\
	const decltype( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})
#else
#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})
#endif

struct MSDReadCommand {
  uint32_t start_block;
  uint32_t num_blocks;
  void *dst;
  void (*completion)(int, struct MSDReadCommand *);
  union {
    struct {
      struct SDCommand sdcommand;
    } sdcard;
    //usb specific Command struct as well
  };
};

struct MSDWriteCommand {
  uint32_t start_block;
  uint32_t num_blocks;
  void const *src;
  void (*completion)(int, struct MSDWriteCommand *);
  union {
    struct {
      struct SDCommand sdcommand;
    } sdcard;
    //usb specific Command struct as well
  };
};

struct MSD_Info {
  uint32_t size;
  uint32_t block_size;
  void *data;

  void (*readBlocks)(void *data, struct MSDReadCommand *command);
  void (*writeBlocks)(void *data, struct MSDWriteCommand *command);
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
