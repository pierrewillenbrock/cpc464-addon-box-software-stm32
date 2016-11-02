
#include "fdc.h"

#include "fpga_comm.h"
#include "sprite.h"
#include "timer.h"
#include "dsk.hpp"

#include <string.h>
#include <stdint.h>
#include <assert.h>

struct FDDCommand {
	uint8_t driveUnit:2;
	uint8_t PHN:1;
	uint8_t command:3;
	uint8_t mfm:1;
	uint8_t valid:1;
	uint8_t PCN; //physical cylinder
	uint8_t C; //logical cylinder
	uint8_t H; //logical head
	uint8_t R; //logical sector size
	uint8_t N; //logical sector number
	uint8_t configuredSectorSize;
	uint8_t sectorsPerTrack;
	uint8_t gapLength;
	uint8_t fillerByte;
};
struct FDDResponse {
	uint8_t reserved:1;
	uint8_t controlMark:1;
	uint8_t badCylinder:1;
	uint8_t wrongCylinder:1;
	uint8_t missingAddressMark:1;
	uint8_t noData:1;
	uint8_t dataError:1;
	uint8_t valid:1;
};
static struct FDDInfoBlock {
	FDDCommand command;
	FDDResponse response;
	uint8_t motorOn;
	uint8_t debug;
	uint8_t reserved[3];
	struct FDDStatus {
		uint8_t reserved:4;
		uint8_t two_sided:1;
		uint8_t fault:1;
		uint8_t write_protected:1;
		uint8_t ready:1;
	} driveStatus[4];
	uint8_t driveNCN[4];
} fddInfoBlock;
static FPGAComm_Command driveStatusFPGACommand;
static uint16_t driveStatus_spriteMap[] = {
	// high 6 bits of address, tile is at 0x6000+0x800
	0x20, 0x22,
	0x21, 0x23,
};
static int driveStatusState = 0;

static RefPtr<Disk> images[4];

static void driveStatusCompletion(int result, struct FPGAComm_Command *unused) {
	if (result != 0) {
		driveStatusState = 0;
		return;
	}
	switch(driveStatusState) {
	case 1:
		if (fddInfoBlock.motorOn & 1) {
			driveStatus_spriteMap[0] = 0x20;
			driveStatus_spriteMap[1] = 0x22;
			driveStatus_spriteMap[2] = 0x21;
			driveStatus_spriteMap[3] = 0x23;
			if (fddInfoBlock.command.valid) {
				driveStatus_spriteMap[3] = 0x24 + fddInfoBlock.command.driveUnit;
			}
		} else {
			driveStatus_spriteMap[0] = 0x3c;
			driveStatus_spriteMap[1] = 0x3c;
			driveStatus_spriteMap[2] = 0x3c;
			driveStatus_spriteMap[3] = 0x3c;
		}
		driveStatusFPGACommand.address = 0x6000;
		driveStatusFPGACommand.length = sizeof(driveStatus_spriteMap);
		driveStatusFPGACommand.read_data = NULL;
		driveStatusFPGACommand.write_data = &driveStatus_spriteMap;
		driveStatusFPGACommand.completion = driveStatusCompletion;
		driveStatusState = 2;
		FPGAComm_ReadWriteCommand(&driveStatusFPGACommand);
		for(unsigned drive = 0; drive < 4; drive++) {
			if (images[drive])
				images[drive]->preloadCylinder
					(fddInfoBlock.driveNCN[drive]);
		}
		break;
	case 2:
		driveStatusState = 0;
		break;
	}
}
static void driveStatusTimer(void *unused) {
	if (driveStatusState != 0)
		return;
	driveStatusFPGACommand.address = 0x4800;
	driveStatusFPGACommand.length = sizeof(fddInfoBlock);
	driveStatusFPGACommand.read_data = &fddInfoBlock;
	driveStatusFPGACommand.write_data = NULL;
	driveStatusFPGACommand.completion = driveStatusCompletion;
	driveStatusState = 1;
	FPGAComm_ReadWriteCommand(&driveStatusFPGACommand);
}

static FDDCommand fdcirq_command;
static FDDCommand fdcirq_command_final;
static FDDResponse fdcirq_response;
static FPGAComm_Command fdcirq_FPGACommand;
static FPGAComm_Command fdcirq_FPGACommand2;
static FPGAComm_Command fdcirq_endisable_FPGACommand;
static DiskFindSectorCommand fdcirq_findsectorcommand;
static enum {
	IDLE,
	COMMAND_FETCH,
	SECTOR_FETCH,
	WAIT_TRANSFERDONE,
	COMMAND_FINAL_FETCH,
} fdcirq_state = IDLE;
static RefPtr<Disk> fdcirq_dskimage;

static uint8_t sectorbuf[2048];

static void fdcirq_FPGACommCompletion(int result,
				      struct FPGAComm_Command *unused);
/*
  communication protocol between stm32 and fdc frontend:
  * frontend pulls command.valid high, which also results in an irq. the
    frontend is paused in execution state waiting for response.valid to go high
  * stm32 prepares memory(and response) as needed and pulls
    response.valid high. this also clears the irq.
  * frontend begins command execution
  * frontend pulls command.valid low and clears command.command when it
    is done executing, which also results in an irq.
  * stm32 reads command
  * the frontend pauses in execution state waiting for response.valid
    to go low.
  * stm32 reads memory and pulls response.valid low when it is done, which
    clears irq.
*/
static void fdcirq_DiskFindSectorCompletion(RefPtr<DiskSector> sector,
					   DiskFindSectorCommand *command) {
	switch (fdcirq_state) {
	case SECTOR_FETCH:
		switch (fdcirq_command.command) {
		case 0: //no command, cannot happen.
			assert(0);
			break;
		case 1: //read id
			if (!sector) {
				//setup response
				fdcirq_response.reserved = 0;
				fdcirq_response.controlMark = 0;
				fdcirq_response.badCylinder = 0;
				fdcirq_response.wrongCylinder = 0;
				fdcirq_response.missingAddressMark = 1;
				fdcirq_response.noData = 1;
				fdcirq_response.dataError = 0;
				fdcirq_response.valid = 1;
			} else {
				sectorbuf[0] = sector->C;
				sectorbuf[1] = sector->H;
				sectorbuf[2] = sector->R;
				sectorbuf[3] = sector->N;
				fdcirq_FPGACommand2.address = 0x4000;
				fdcirq_FPGACommand2.length = 4;
				fdcirq_FPGACommand2.read_data = NULL;
				fdcirq_FPGACommand2.write_data = &sectorbuf;
				//no completion needed
				fdcirq_FPGACommand2.completion = NULL;
				FPGAComm_ReadWriteCommand(&fdcirq_FPGACommand2);

				fdcirq_response.reserved = 0;
				fdcirq_response.controlMark = (sector->ST2 & 0x80)?1:0;
				fdcirq_response.badCylinder = (sector->ST2 & 0x02)?1:0;
				fdcirq_response.wrongCylinder = (sector->ST2 & 0x10)?1:0;
				fdcirq_response.missingAddressMark = (sector->ST2 & 0x01)?1:0;
				fdcirq_response.noData = (sector->ST1 & 0x04)?1:0;
				fdcirq_response.dataError = (sector->ST1 & 0x20)?1:0;
				fdcirq_response.valid = 1;
			}
			fdcirq_FPGACommand.address = 0x480a;
			fdcirq_FPGACommand.length = 1;
			fdcirq_FPGACommand.read_data = NULL;
			fdcirq_FPGACommand.write_data = &fdcirq_response;
			fdcirq_FPGACommand.completion = NULL;
			FPGAComm_ReadWriteCommand(&fdcirq_FPGACommand);

			fdcirq_endisable_FPGACommand.completion = NULL;
			FPGAComm_EnableIRQs_nb(0x01, &fdcirq_endisable_FPGACommand);
			fdcirq_state = WAIT_TRANSFERDONE;
			break;
		case 3: //read data
			if (!sector) {
				//setup response
				fdcirq_response.reserved = 0;
				fdcirq_response.controlMark = 0;
				fdcirq_response.badCylinder = 0;
				fdcirq_response.wrongCylinder = 0;
				fdcirq_response.missingAddressMark = 1;
				fdcirq_response.noData = 1;
				fdcirq_response.dataError = 0;
				fdcirq_response.valid = 1;
			} else {
				//todo: need to copy data to sectorbuf
				memcpy(sectorbuf, sector->data, sector->size);

				fdcirq_FPGACommand2.address = 0x4000;
				fdcirq_FPGACommand2.length = sector->size;
				fdcirq_FPGACommand2.read_data = NULL;
				fdcirq_FPGACommand2.write_data = &sectorbuf;
				//no completion needed
				fdcirq_FPGACommand2.completion = NULL;
				FPGAComm_ReadWriteCommand(&fdcirq_FPGACommand2);

				fdcirq_response.reserved = 0;
				fdcirq_response.controlMark = (sector->ST2 & 0x80)?1:0;
				fdcirq_response.badCylinder = (sector->ST2 & 0x02)?1:0;
				fdcirq_response.wrongCylinder = (sector->ST2 & 0x10)?1:0;
				fdcirq_response.missingAddressMark = (sector->ST2 & 0x01)?1:0;
				fdcirq_response.noData = (sector->ST1 & 0x04)?1:0;
				fdcirq_response.dataError = (sector->ST1 & 0x20)?1:0;
				fdcirq_response.valid = 1;
			}
			fdcirq_FPGACommand.address = 0x480a;
			fdcirq_FPGACommand.length = 1;
			fdcirq_FPGACommand.read_data = NULL;
			fdcirq_FPGACommand.write_data = &fdcirq_response;
			fdcirq_FPGACommand.completion = NULL;
			FPGAComm_ReadWriteCommand(&fdcirq_FPGACommand);

			fdcirq_endisable_FPGACommand.completion = NULL;
			FPGAComm_EnableIRQs_nb(0x01, &fdcirq_endisable_FPGACommand);
			fdcirq_state = WAIT_TRANSFERDONE;
			break;
		default:
			assert(0);
			break;
		}
		break;
	default:
		assert(0);
		break;
	}
}

static void fdcirq_FPGACommCompletion(int result,
				      struct FPGAComm_Command *unused) {
	if (result != 0) {
		//this is bad. retry.
		FPGAComm_ReadWriteCommand(&fdcirq_FPGACommand);
		return;
	}
	switch (fdcirq_state) {
	case COMMAND_FETCH: //we just fetched our fdcirq_command.
		if(!fdcirq_command.valid) {
			fdcirq_endisable_FPGACommand.completion = NULL;
			FPGAComm_EnableIRQs_nb(0x01, &fdcirq_endisable_FPGACommand);
			fdcirq_state = IDLE;
			return;
		}
		fdcirq_dskimage = images[fdcirq_command.driveUnit];
		//formattrack does not need a disk to work.
		//it should not happen while ready is low(should be handled by
		//fpga without intervention), but the implementation is simple
		//enough.
		if (!fdcirq_dskimage && fdcirq_command.command != 2) {
			//command without a disk ready. should be handled in
			//fpga without intervention.
			fdcirq_response.reserved = 0;
			fdcirq_response.controlMark = 0;
			fdcirq_response.badCylinder = 0;
			fdcirq_response.wrongCylinder = 0;
			fdcirq_response.missingAddressMark = 1;
			fdcirq_response.noData = 0;
			fdcirq_response.dataError = 0;
			fdcirq_response.valid = 1;
			fdcirq_FPGACommand.address = 0x480a;
			fdcirq_FPGACommand.length = 1;
			fdcirq_FPGACommand.read_data = NULL;
			fdcirq_FPGACommand.write_data = &fdcirq_response;
			fdcirq_FPGACommand.completion = NULL;
			FPGAComm_ReadWriteCommand(&fdcirq_FPGACommand);

			fdcirq_endisable_FPGACommand.completion = NULL;
			FPGAComm_EnableIRQs_nb(0x01, &fdcirq_endisable_FPGACommand);
			fdcirq_state = WAIT_TRANSFERDONE;
		} else {
			switch (fdcirq_command.command) {
			case 0: //no command, cannot happen.
				assert(0);
				break;
			case 1: //read id
				fdcirq_findsectorcommand.pcn =
					fdcirq_command.PCN;
				fdcirq_findsectorcommand.phn =
					fdcirq_command.PHN;
				fdcirq_findsectorcommand.mfm =
					fdcirq_command.mfm != 0;
				fdcirq_findsectorcommand.C =
					fdcirq_command.C;
				fdcirq_findsectorcommand.H =
					fdcirq_command.H;
				fdcirq_findsectorcommand.R =
					fdcirq_command.R;
				fdcirq_findsectorcommand.N =
					fdcirq_command.N;
				fdcirq_findsectorcommand.deleted = false;
				fdcirq_findsectorcommand.find_any = true;
				fdcirq_findsectorcommand.completion =
					fdcirq_DiskFindSectorCompletion;
				fdcirq_state = SECTOR_FETCH;
				fdcirq_dskimage->findSector
					(&fdcirq_findsectorcommand);
				break;
			case 3: //READDATA
				fdcirq_findsectorcommand.pcn =
					fdcirq_command.PCN;
				fdcirq_findsectorcommand.phn =
					fdcirq_command.PHN;
				fdcirq_findsectorcommand.mfm =
					fdcirq_command.mfm != 0;
				fdcirq_findsectorcommand.C =
					fdcirq_command.C;
				fdcirq_findsectorcommand.H =
					fdcirq_command.H;
				fdcirq_findsectorcommand.R =
					fdcirq_command.R;
				fdcirq_findsectorcommand.N =
					fdcirq_command.N;
				fdcirq_findsectorcommand.deleted = false;
				fdcirq_findsectorcommand.find_any = false;
				fdcirq_findsectorcommand.completion =
					fdcirq_DiskFindSectorCompletion;
				fdcirq_state = SECTOR_FETCH;
				fdcirq_dskimage->findSector
					(&fdcirq_findsectorcommand);
				break;
			default:
				assert(0);
				break;
			}
		}
		break;
	case COMMAND_FINAL_FETCH:
		assert(!fdcirq_command_final.valid);
		if (!fdcirq_dskimage && fdcirq_command.command != 2) {
			fdcirq_response.valid = 0;
			fdcirq_FPGACommand.address = 0x480a;
			fdcirq_FPGACommand.length = 1;
			fdcirq_FPGACommand.read_data = NULL;
			fdcirq_FPGACommand.write_data = &fdcirq_response;
			fdcirq_FPGACommand.completion = NULL;
			FPGAComm_ReadWriteCommand(&fdcirq_FPGACommand);
			fdcirq_endisable_FPGACommand.completion = NULL;
			FPGAComm_EnableIRQs_nb(0x01, &fdcirq_endisable_FPGACommand);
			fdcirq_state = IDLE;
		} else {
			switch (fdcirq_command.command) {
			case 0: //no command, cannot happen.
				assert(0);
				break;
			case 1: //read id
			case 3: //read data
				fdcirq_response.valid = 0;
				fdcirq_FPGACommand.address = 0x480a;
				fdcirq_FPGACommand.length = 1;
				fdcirq_FPGACommand.read_data = NULL;
				fdcirq_FPGACommand.write_data = &fdcirq_response;
				fdcirq_FPGACommand.completion = NULL;
				FPGAComm_ReadWriteCommand(&fdcirq_FPGACommand);
				fdcirq_endisable_FPGACommand.completion = NULL;
				FPGAComm_EnableIRQs_nb(0x01, &fdcirq_endisable_FPGACommand);
				fdcirq_state = IDLE;
				break;
			default:
				assert(0);
				break;
			}
		}
		break;
	default:
		assert(0);
		break;
	}
}

static void FDC_IRQHandler(void *unused) {
	switch(fdcirq_state) {
	case IDLE:
		//first, queue the disable command so we can retrigger the irq when we reenable and it is asserted again already.
		fdcirq_endisable_FPGACommand.completion = NULL;
		FPGAComm_DisableIRQs_nb(0x01, &fdcirq_endisable_FPGACommand);
		//need to fetch the command
		fdcirq_FPGACommand.address = 0x4800;
		fdcirq_FPGACommand.length = sizeof(fdcirq_command);
		fdcirq_FPGACommand.read_data = &fdcirq_command;
		fdcirq_FPGACommand.write_data = NULL;
		fdcirq_FPGACommand.completion = fdcirq_FPGACommCompletion;
		fdcirq_state = COMMAND_FETCH;
		FPGAComm_ReadWriteCommand(&fdcirq_FPGACommand);
		break;
	case WAIT_TRANSFERDONE:
		//first, queue the disable command so we can retrigger the irq when we reenable and it is asserted again already.
		fdcirq_endisable_FPGACommand.completion = NULL;
		FPGAComm_DisableIRQs_nb(0x01, &fdcirq_endisable_FPGACommand);
		//need to fetch the command. contains final register states.
		fdcirq_FPGACommand.address = 0x4800;
		fdcirq_FPGACommand.length = sizeof(fdcirq_command);
		fdcirq_FPGACommand.read_data = &fdcirq_command_final;
		fdcirq_FPGACommand.write_data = NULL;
		fdcirq_FPGACommand.completion = fdcirq_FPGACommCompletion;
		fdcirq_state = COMMAND_FINAL_FETCH;
		FPGAComm_ReadWriteCommand(&fdcirq_FPGACommand);
		break;
	default:
		//ignore spurious irqs.
		break;
	}
}

void FDC_Setup() {
	//put some tile data in graphics ram
#define TILE_LINE(c1, c2, c3, c4, c5, c6, c7, c8, p12, p34, p56, p78)	\
	((p12) << 8) |  ((c2) << 4) | (c1),				\
		((p34) << 8) |  ((c4) << 4) | (c3),			\
		((p56) << 8) |  ((c6) << 4) | (c5),			\
		((p78) << 8) |  ((c8) << 4) | (c7)
	uint16_t tiles[] = {
		//left top corner
		TILE_LINE( 5, 5, 5, 5, 5, 5, 5, 5,0,0,0,0),
		TILE_LINE( 5, 4, 4, 4, 4, 4, 4, 4,0,0,0,0),
		TILE_LINE( 5, 4, 7, 7, 7, 4, 7, 7,0,0,0,0),
		TILE_LINE( 5, 4, 6, 6, 6, 6, 6, 6,0,0,0,0),
		TILE_LINE( 5, 4, 4, 4, 4, 4, 4, 4,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 5, 5, 5,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 5, 5, 5,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 5, 4, 5,0,0,0,0),
		//left bottom corner
		TILE_LINE( 5, 5, 5, 5, 5, 4, 4, 4,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 5, 4, 5,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 5, 5, 5,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 5, 6, 5,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 6, 6, 6,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 6, 6, 6,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 5, 6, 5,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 5, 5, 5,0,0,0,0),
		//right top corner
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 4, 4, 4, 4, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 7, 7, 4, 4, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 6, 6, 6, 4, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 4, 4, 4, 4, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		//right bottom corner
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		//right bottom corner A
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 8, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 8, 0, 8, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 8, 8, 8, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 8, 0, 8, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 8, 0, 8, 0,0,0,0,0),
		//right bottom corner B
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 8, 8, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 8, 0, 8, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 8, 8, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 8, 0, 8, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 8, 8, 0, 0,0,0,0,0),
		//right bottom corner C
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 8, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 8, 0, 8, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 8, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 8, 0, 8, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 8, 0, 0,0,0,0,0),
		//right bottom corner D
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 5, 0, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 8, 8, 0, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 8, 0, 8, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 8, 0, 8, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 8, 0, 8, 0,0,0,0,0),
		TILE_LINE( 5, 5, 5, 5, 8, 8, 0, 0,0,0,0,0),
		};
	FPGAComm_CopyToFPGA(0x6000 + 0x20*0x40, tiles, sizeof(tiles));

	//put some map data in graphics ram
	FPGAComm_CopyToFPGA(0x6000, driveStatus_spriteMap, sizeof(driveStatus_spriteMap));

	sprite_info disc_sprite = {
		.hpos = 180,
		.vpos = 64,
		.map_addr = 0,
		.hsize = 2,
		.vsize = 2,
		.hpitch = 2,
		.doublesize = 0
	};
	FPGAComm_CopyToFPGA(0x7000, &disc_sprite, sizeof(disc_sprite));


	Timer_Repeating(40000, driveStatusTimer, NULL);
	FPGAComm_SetIRQHandler(0, FDC_IRQHandler, NULL);
	FPGAComm_EnableIRQs(0x01);
}

void FDC_InsertDisk(int drive, char const *filename) {
	assert(drive >= 0 && drive < 4);
	images[drive] = DSK_openImage(filename);
	if (!images[drive])
		return;
	uint8_t b;
	if (images[drive]->write_protected)
		b = 0xc0;
	else
		b = 0x80;
	FPGAComm_CopyToFPGA(0x4810 + drive, (void*)&b, 1);
}

void FDC_EjectDisk(int drive) {
	assert(drive >= 0 && drive < 4);
	uint8_t b;
	b = 0x00;
	FPGAComm_CopyToFPGA(0x4810 + drive, (void*)&b, 1);
	if (images[drive])
		images[drive]->close();
	images[drive] = NULL;
}
