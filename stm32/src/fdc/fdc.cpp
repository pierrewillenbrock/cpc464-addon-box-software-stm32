
#include <fdc/fdc.h>

#include <fpga/fpga_comm.hpp>
#include <fpga/layout.h>
#include <timer.hpp>
#include <fdc/dsk.hpp>

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
static int driveStatusState = 0;
static uint8_t driveLastMotorState = 0;
static uint8_t driveLastAccessState = 0;
static uint8_t driveAccessCount[4] = {0,0,0,0};

static RefPtr<dsk::Disk> images[4];

static void driveStatusCompletion(int result) {
	driveStatusState = 0;
	if (result != 0) {
		return;
	}
	if ((fddInfoBlock.motorOn & 1) && !driveLastMotorState) {
		driveLastMotorState = 1;
		FDC_MotorOn();
	}
	if (!(fddInfoBlock.motorOn & 1) && driveLastMotorState) {
		driveLastMotorState = 0;
		FDC_MotorOff();
	}
}

static void driveStatusTimer() {
	for(unsigned i = 0; i < 4; i++) {
		if (driveAccessCount[i] > 0) {
			driveAccessCount[i]--;
			if (!(driveLastAccessState & (1 << i))) {
				driveLastAccessState |= 1 << i;
				FDC_Activity(i, 1);
			}
		} else {
			if ((driveLastAccessState & (1 << i))) {
				driveLastAccessState &= ~(1 << i);
				FDC_Activity(i, 0);
			}
		}
	}

	if (driveStatusState != 0)
		return;
	driveStatusFPGACommand.address = FPGA_CPC_FDC_INFOBLK;
	driveStatusFPGACommand.length = sizeof(fddInfoBlock);
	driveStatusFPGACommand.read_data = &fddInfoBlock;
	driveStatusFPGACommand.write_data = NULL;
	driveStatusFPGACommand.slot = sigc::ptr_fun(&driveStatusCompletion);
	driveStatusState = 1;
	FPGAComm_ReadWriteCommand(&driveStatusFPGACommand);
}

static FDDCommand fdcirq_command;
static FDDCommand fdcirq_command_final;
static FDDResponse fdcirq_response;
static FPGAComm_Command fdcirq_FPGACommand;
static FPGAComm_Command fdcirq_FPGACommand2;
static FPGAComm_Command fdcirq_endisable_FPGACommand;
static dsk::DiskFindSectorCommand fdcirq_findsectorcommand;
static enum {
	IDLE,
	COMMAND_FETCH,
	SECTOR_FETCH,
	WAIT_TRANSFERDONE,
	COMMAND_FINAL_FETCH,
} fdcirq_state = IDLE;
static RefPtr<dsk::Disk> fdcirq_dskimage;

static uint8_t sectorbuf[2048];

static void fdcirq_FPGACommCompletion(int result);
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
static void fdcirq_DiskFindSectorCompletion(RefPtr<dsk::DiskSector> sector) {
	driveAccessCount[fdcirq_command.driveUnit] = 10;
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
				fdcirq_FPGACommand2.address = FPGA_CPC_FDC_DATA;
				fdcirq_FPGACommand2.length = 4;
				fdcirq_FPGACommand2.read_data = NULL;
				fdcirq_FPGACommand2.write_data = &sectorbuf;
				//no slot needed
				fdcirq_FPGACommand2.slot = sigc::slot<void(int)>();
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
			fdcirq_FPGACommand.address = FPGA_CPC_FDC_INSTS;
			fdcirq_FPGACommand.length = 1;
			fdcirq_FPGACommand.read_data = NULL;
			fdcirq_FPGACommand.write_data = &fdcirq_response;
			fdcirq_FPGACommand.slot = sigc::slot<void(int)>();
			FPGAComm_ReadWriteCommand(&fdcirq_FPGACommand);

			fdcirq_endisable_FPGACommand.slot = sigc::slot<void(int)>();
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
				//copy data to sectorbuf
				memcpy(sectorbuf, sector->data, sector->size);

				fdcirq_FPGACommand2.address = FPGA_CPC_FDC_DATA;
				fdcirq_FPGACommand2.length = sector->size;
				fdcirq_FPGACommand2.read_data = NULL;
				fdcirq_FPGACommand2.write_data = &sectorbuf;
				//no slot needed
				fdcirq_FPGACommand2.slot = sigc::slot<void(int)>();
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
			fdcirq_FPGACommand.address = FPGA_CPC_FDC_INSTS;
			fdcirq_FPGACommand.length = 1;
			fdcirq_FPGACommand.read_data = NULL;
			fdcirq_FPGACommand.write_data = &fdcirq_response;
			fdcirq_FPGACommand.slot = sigc::slot<void(int)>();
			FPGAComm_ReadWriteCommand(&fdcirq_FPGACommand);

			fdcirq_endisable_FPGACommand.slot = sigc::slot<void(int)>();
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

static void fdcirq_FPGACommCompletion(int result) {
	if (result != 0) {
		//this is bad. retry.
		FPGAComm_ReadWriteCommand(&fdcirq_FPGACommand);
		return;
	}
	driveAccessCount[fdcirq_command.driveUnit] = 10;
	switch (fdcirq_state) {
	case COMMAND_FETCH: //we just fetched our fdcirq_command.
		if(!fdcirq_command.valid) {
			fdcirq_endisable_FPGACommand.slot = sigc::slot<void(int)>();
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
			fdcirq_FPGACommand.address = FPGA_CPC_FDC_INSTS;
			fdcirq_FPGACommand.length = 1;
			fdcirq_FPGACommand.read_data = NULL;
			fdcirq_FPGACommand.write_data = &fdcirq_response;
			fdcirq_FPGACommand.slot = sigc::slot<void(int)>();
			FPGAComm_ReadWriteCommand(&fdcirq_FPGACommand);

			fdcirq_endisable_FPGACommand.slot = sigc::slot<void(int)>();
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
				fdcirq_findsectorcommand.slot =
					sigc::ptr_fun(&fdcirq_DiskFindSectorCompletion);
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
				fdcirq_findsectorcommand.slot =
					sigc::ptr_fun(&fdcirq_DiskFindSectorCompletion);
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
			fdcirq_FPGACommand.address = FPGA_CPC_FDC_INSTS;
			fdcirq_FPGACommand.length = 1;
			fdcirq_FPGACommand.read_data = NULL;
			fdcirq_FPGACommand.write_data = &fdcirq_response;
			fdcirq_FPGACommand.slot = sigc::slot<void(int)>();
			FPGAComm_ReadWriteCommand(&fdcirq_FPGACommand);
			fdcirq_endisable_FPGACommand.slot = sigc::slot<void(int)>();
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
				fdcirq_FPGACommand.address = FPGA_CPC_FDC_INSTS;
				fdcirq_FPGACommand.length = 1;
				fdcirq_FPGACommand.read_data = NULL;
				fdcirq_FPGACommand.write_data = &fdcirq_response;
				fdcirq_FPGACommand.slot = sigc::slot<void(int)>();
				FPGAComm_ReadWriteCommand(&fdcirq_FPGACommand);
				fdcirq_endisable_FPGACommand.slot = sigc::slot<void(int)>();
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

static void FDC_IRQHandler() {
	switch(fdcirq_state) {
	case IDLE:
		//first, queue the disable command so we can retrigger the irq when we reenable and it is asserted again already.
		fdcirq_endisable_FPGACommand.slot = sigc::slot<void(int)>();
		FPGAComm_DisableIRQs_nb(0x01, &fdcirq_endisable_FPGACommand);
		//need to fetch the command
		fdcirq_FPGACommand.address = FPGA_CPC_FDC_INFOBLK;
		fdcirq_FPGACommand.length = sizeof(fdcirq_command);
		fdcirq_FPGACommand.read_data = &fdcirq_command;
		fdcirq_FPGACommand.write_data = NULL;
		fdcirq_FPGACommand.slot = sigc::ptr_fun(&fdcirq_FPGACommCompletion);
		fdcirq_state = COMMAND_FETCH;
		FPGAComm_ReadWriteCommand(&fdcirq_FPGACommand);
		break;
	case WAIT_TRANSFERDONE:
		//first, queue the disable command so we can retrigger the irq when we reenable and it is asserted again already.
		fdcirq_endisable_FPGACommand.slot = sigc::slot<void(int)>();
		FPGAComm_DisableIRQs_nb(0x01, &fdcirq_endisable_FPGACommand);
		//need to fetch the command. contains final register states.
		fdcirq_FPGACommand.address = FPGA_CPC_FDC_INFOBLK;
		fdcirq_FPGACommand.length = sizeof(fdcirq_command);
		fdcirq_FPGACommand.read_data = &fdcirq_command_final;
		fdcirq_FPGACommand.write_data = NULL;
		fdcirq_FPGACommand.slot = sigc::ptr_fun(&fdcirq_FPGACommCompletion);
		fdcirq_state = COMMAND_FINAL_FETCH;
		FPGAComm_ReadWriteCommand(&fdcirq_FPGACommand);
		break;
	default:
		//ignore spurious irqs.
		break;
	}
}

void FDC_Setup() {
	Timer_Repeating(40000, sigc::ptr_fun(&driveStatusTimer));
	FPGAComm_IRQHandler(0).connect(sigc::ptr_fun(&FDC_IRQHandler));
	FPGAComm_EnableIRQs(0x01);
}

void FDC_InsertDisk(int drive, char const *filename) {
	assert(drive >= 0 && drive < 4);
	images[drive] = dsk::openImage(filename);
	if (!images[drive])
		return;
	uint8_t b;
	if (images[drive]->write_protected)
		b = 0xc0;
	else
		b = 0x80;
	FPGAComm_CopyToFPGA(FPGA_CPC_FDC_FDD_STS(drive), (void*)&b, 1);
}

void FDC_EjectDisk(int drive) {
	assert(drive >= 0 && drive < 4);
	uint8_t b;
	b = 0x00;
	FPGAComm_CopyToFPGA(FPGA_CPC_FDC_FDD_STS(drive), (void*)&b, 1);
	if (images[drive])
		images[drive]->close();
	images[drive] = NULL;
}
