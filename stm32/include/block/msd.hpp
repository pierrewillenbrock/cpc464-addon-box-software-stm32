
#pragma once

#include <stdint.h>
#include "sdio.hpp"
#include "bits.h"
#include <sigc++/sigc++.h>

struct MSDReadCommand {
  uint32_t start_block;
  uint32_t num_blocks;
  void *dst;
  sigc::slot<void(int)> slot;
  struct {
    struct SDCommand sdcommand;
  } sdcard;
  //usb specific Command struct as well
};

struct MSDWriteCommand {
  uint32_t start_block;
  uint32_t num_blocks;
  void const *src;
  sigc::slot<void(int)> slot;
  struct {
    struct SDCommand sdcommand;
  } sdcard;
  //usb specific Command struct as well
};

class MSD {
public:
  uint32_t size;
  uint32_t block_size;
  MSD(uint32_t block_size) : size(0), block_size(block_size) {}
  MSD(uint32_t size, uint32_t block_size) : size(size), block_size(block_size) {}
  virtual void readBlocks(struct MSDReadCommand *command) = 0;
  virtual void writeBlocks(struct MSDWriteCommand *command) = 0;
};

class FilesystemDriver {
public:
  virtual void probe_partition(uint32_t type, uint32_t first_block,
                               uint32_t num_blocks, MSD *msd) = 0;
  virtual void remove_msd(MSD *msd) = 0;
};

void MSD_Register(struct MSD *msd);
void MSD_Unregister(struct MSD *msd);
void FSDriver_Register(struct FilesystemDriver *drv);

