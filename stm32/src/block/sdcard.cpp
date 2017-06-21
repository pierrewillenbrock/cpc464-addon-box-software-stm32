
#include <block/sdcard.h>

#include <stdlib.h>
#include <errno.h>

#include <bsp/stm32f4xx_gpio.h>
#include <bsp/stm32f4xx_rcc.h>
#include <bsp/stm32f4xx_exti.h>
#include <bsp/stm32f4xx_syscfg.h>
#include <irq.h>
#include <timer.hpp>
#include <block/msd.hpp>
#include <block/sdio.hpp>
#include <deque>

#include "sdcard_std.h"
#include <hw/sd.h>

int card_present = 0;
static uint32_t sdcard_timer_handle = 0;
enum { CardNotKnown, Card1, Card2, CardHC } card_type = CardNotKnown;
static int volt_retry = 0;
union {
	uint32_t d[4];
	struct {
		uint32_t one:1;
		uint32_t crc:7;
		uint32_t Manufacturing_date:12;
		uint32_t rsvd:4;
		uint32_t Product_serial_number:32;
		uint32_t Product_revision:8;
		char Product_name[5];
		uint32_t OEM_APP_ID:16;
		uint32_t manufactureer_ID:8;
	} __attribute__((packed));
} card_CID;
union {
	uint32_t d[4];
	struct {
		uint32_t one:1;
		uint32_t crc:7;
		uint32_t rsvd1:2;
		uint32_t file_format:2;
		uint32_t temporary_write_protect:1;
		uint32_t permanent_write_protect:1;
		uint32_t copy_flag:1;
		uint32_t file_format_group:1;
		uint32_t rsvd2:5;
		uint32_t partial_blocks_for_write_allowed:1;
		uint32_t max_write_data_block_length:4;
		uint32_t write_speed_factor:3;
		uint32_t rsvd3:2;
		uint32_t write_protect_group_enable:1;
		uint32_t write_protect_group_size:7;
		uint32_t erase_sector_size:7;
		uint32_t erase_single_block_enable:1;
		uint32_t device_size_multiplier:3;
		uint32_t max_write_current_vdd_max:3;
		uint32_t max_write_current_vdd_min:3;
		uint32_t max_read_current_vdd_max:3;
		uint32_t max_read_current_vdd_min:3;
		uint32_t device_size:12;
		uint32_t rsvd4:2;
		uint32_t DSR_implemented:1;
		uint32_t read_block_misalignment:1;
		uint32_t write_block_misalignment:1;
		uint32_t partial_blocks_for_read_allowed:1;
		uint32_t max_read_data_block_length:4;
		uint32_t card_command_class:12;
		uint32_t max_data_transfer_rate:8;
		uint32_t data_read_access_time_2:8;
		uint32_t data_read_access_time_1:8;
		uint32_t rsvd5:6;
		uint32_t CSD_structure:2;
	} __attribute__((packed)) v1;
	struct {
		uint32_t one:1;
		uint32_t crc:7;
		uint32_t rsvd1:2;
		uint32_t file_format:2;
		uint32_t temporary_write_protect:1;
		uint32_t permanent_write_protect:1;
		uint32_t copy_flag:1;
		uint32_t file_format_group:1;
		uint32_t rsvd2:5;
		uint32_t partial_blocks_for_write_allowed:1;
		uint32_t max_write_data_block_length:4;
		uint32_t write_speed_factor:3;
		uint32_t rsvd3:2;
		uint32_t write_protect_group_enable:1;
		uint32_t write_protect_group_size:7;
		uint32_t erase_sector_size:7;
		uint32_t erase_single_block_enable:1;
		uint32_t rsvd4:1;
		uint32_t device_size:22;
		uint32_t rsvd5:6;
		uint32_t DSR_implemented:1;
		uint32_t read_block_misalignment:1;
		uint32_t write_block_misalignment:1;
		uint32_t partial_blocks_for_read_allowed:1;
		uint32_t max_read_data_block_length:4;
		uint32_t card_command_class:12;
		uint32_t max_data_transfer_rate:8;
		uint32_t data_read_access_time_2:8;
		uint32_t data_read_access_time_1:8;
		uint32_t rsvd6:6;
		uint32_t CSD_structure:2;
	} __attribute__((packed)) v2;
} card_CSD;
union {
	uint8_t d[8]; //card sends MSB first, we need LSB first here.
	uint32_t e[2];
	struct {
		uint32_t reserved_mfg;
		uint32_t reserved:16;
		uint32_t sd_bus_widths:4;//bit 0: supports 1 bit, bit2: supports 4 bits. rest reserved.
		uint32_t sd_security:3; //0: none, 1: unused, 2: v1.01, 3: v2.00, rest reserved.
		uint32_t data_status_after_erase:1;
		uint32_t SD_spec:4; //0:1.0-1.01, 1: 1.10, 2: 2.0
		uint32_t SCR_structure:4; //0: v1
	} __attribute__((packed)) v1;
} card_SCR;
static uint16_t card_rca;
static int card_khz;

void SDcard_Setup() {
	SDIO_Setup();

	RCC_AHB1PeriphClockCmd(SD_PWR_CD_GPIO_RCC, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);

	GPIO_InitTypeDef gpio_init;
	GPIO_SetBits(SD_PWR_GPIO, SD_PWR_PIN);//keep it off.
	GPIO_StructInit(&gpio_init);
	gpio_init.GPIO_Pin = SD_PWR_PIN;
	gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_Init(SD_PWR_GPIO, &gpio_init);

	gpio_init.GPIO_Pin = SD_CD_PIN;
	gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_IN;
	gpio_init.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(SD_CD_GPIO, &gpio_init);
	SYSCFG_EXTILineConfig(SD_CD_EXTI_PortSourceGPIO, SD_CD_EXTI_PinSource);

	//setup CD pin edge interrupt
	//check CD pin, then check if we already initialized, if not, initialize sd card. then, fix the edge interrupt and check cd again. if it changed, deinitialize, fix the edge interrupt aso.

	uint32_t irq_save;
	ISR_Disable(irq_save);
	EXTI_InitTypeDef extiinit;
	EXTI_StructInit(&extiinit);
	extiinit.EXTI_Line = SD_CD_EXTI_Line;
	extiinit.EXTI_Mode = EXTI_Mode_Interrupt;
	extiinit.EXTI_Trigger = EXTI_Trigger_Falling;
	extiinit.EXTI_LineCmd = ENABLE;
	EXTI_Init(&extiinit);

	if (GPIO_ReadInputDataBit(SD_CD_GPIO, SD_CD_PIN) == Bit_RESET)
		EXTI_GenerateSWInterrupt(SD_CD_EXTI_Line);

	ISR_Enable(irq_save);

	NVIC_InitTypeDef nvicinit;
	nvicinit.NVIC_IRQChannel = EXTI9_5_IRQn;
	nvicinit.NVIC_IRQChannelPreemptionPriority = 3;
	nvicinit.NVIC_IRQChannelSubPriority = 3;
	nvicinit.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&nvicinit);
}

class SDCard : public MSD {
public:
	SDCard();
	virtual void readBlocks(struct MSDReadCommand *command);
	virtual void writeBlocks(struct MSDWriteCommand *command);
};

SDCard::SDCard()
: MSD(512)
{}

static SDCard sdcard;

static void SDcard_init_power();
static void SDcard_init_clock();
static void SDcard_init_reset();
static void SDcard_init_reset_cmpl(int result);
static void SDcard_init_interface_condition();
static void SDcard_init_interface_condition_cmpl(int result);
static void SDcard_init_sdcard_probe();
static void SDcard_init_sdcard_probe_cmpl(int result);
static void SDcard_init_operation_condition();
static void SDcard_init_operation_condition_cmpl(int result);
static void SDcard_init_operation_condition_timer();
static void SDcard_init_get_CID();
static void SDcard_init_get_CID_cmpl(int result);
static void SDcard_init_set_addr();
static void SDcard_init_set_addr_cmpl(int result);
static void SDcard_init_get_CSD();
static void SDcard_init_get_CSD_cmpl(int result);
static void SDcard_init_select_card();
static void SDcard_init_select_card_cmpl(int result);
static void SDcard_init_get_SCR();
static void SDcard_init_get_SCR_cmpl(int result);
static void SDcard_init_set_blocksize();
static void SDcard_init_set_blocksize_cmpl(int result);
static void SDcard_init_set_bus_width();
static void SDcard_init_set_bus_width_cmpl(int result);
static void SDcard_init_finish();

static void SDcard_init_get_status();
static void SDcard_init_get_status_cmpl(int result);

static void SDcard_init_power() {
	//irq context: cannot wait here for long. need to drive it using irqs.
	//switch pins back to AF
	GPIO_ResetBits(SD_PWR_GPIO, SD_PWR_PIN);//switch it on

	SDIO_PowerUp();

	sdcard_timer_handle = Timer_Oneshot
	(10000, sigc::ptr_fun(&SDcard_init_clock));
}

static void SDcard_init_clock() {
	//irq context: cannot wait here for long. need to drive it using irqs.
	SDIO_ClockEnable();

	SDcard_init_reset();
}

static struct SDCommand command;

static void SDcard_init_reset() {
	command.argument = 0;
	command.command = 0;
	command.responseType = SDResponseType::NoResponse;
	command.dataType = SDDataType::NoData;
	command.retryCounter = 0;
	command.data = NULL;
	command.slot = sigc::ptr_fun(&SDcard_init_reset_cmpl);

	SDIO_Command(&command);
}

static void SDcard_init_reset_cmpl(int result) {
	if (result == SDIO_SystemTimeout)
		return;

	//card is in IDLE state

	SDcard_init_interface_condition();
}

static void SDcard_init_interface_condition() {
	command.argument = 0x000001aa;
	command.command = 8;
	command.responseType = SDResponseType::ResponseShort;
	command.dataType = SDDataType::NoData;
	command.retryCounter = 2;
	command.data = NULL;
	command.slot = sigc::ptr_fun(&SDcard_init_interface_condition_cmpl);

	SDIO_Command(&command);
}

static void SDcard_init_interface_condition_cmpl(int result) {
	if (result != SDIO_OK &&
	    result != SDIO_CommandTimeout) {
		return;
	}

	if (result == SDIO_OK &&
	    (command.response[0] & 0xff) == 0xaa &&
	    (command.response[0] & 0xf00) != 0) {
		//2.0 capable card.
		card_type = Card2;
	} else {
		//not 2.0 capable card.
		card_type = Card1;
	}

	//card is still in IDLE state

	SDcard_init_sdcard_probe();
}

static void SDcard_init_sdcard_probe() {
	command.argument = 0;
	command.command = 55;
	command.responseType = SDResponseType::Response1;
	command.dataType = SDDataType::NoData;
	command.retryCounter = 2;
	command.data = NULL;
	command.slot = sigc::ptr_fun(&SDcard_init_sdcard_probe_cmpl);

	SDIO_Command(&command);
}

static void SDcard_init_sdcard_probe_cmpl(int result) {
	if (result != SDIO_OK) {
		return;
	}

	volt_retry = 0;

	//card is still in IDLE state

	SDcard_init_operation_condition();
}

static void SDcard_init_operation_condition() {
	command.argument = SD_VOLTAGE_WINDOW_SD |
		(card_type == Card2?SD_HIGH_CAPACITY:0);
	command.command = 41 | SDIO_APPCMD;
	command.rca = 0;
	command.responseType = SDResponseType::Response3;//no crc
	command.dataType = SDDataType::NoData;
	command.retryCounter = 2;
	command.data = NULL;
	command.slot = sigc::ptr_fun(&SDcard_init_operation_condition_cmpl);

	SDIO_Command(&command);
}

static void SDcard_init_operation_condition_cmpl(int result) {
	if (result != SDIO_OK &&
	    result != SDIO_SystemTimeout &&
	    result != SDIO_CommandTimeout &&
	    result != SDIO_CommandCRC &&
	    result != SDIO_CommandMismatch &&
	    result != SDIO_CSError) {
		return;
	}

	if (result == SDIO_SystemTimeout ||
	    result == SDIO_CommandTimeout ||
	    result == SDIO_CommandCRC ||
	    result == SDIO_CommandMismatch ||
	    result == SDIO_CSError) {
		volt_retry++;
		if (volt_retry > 10000)
			return;

		sdcard_timer_handle = Timer_Oneshot
		(10000, sigc::ptr_fun(&SDcard_init_operation_condition_timer));
		return;
	}

	uint32_t response = command.response[0];

	if ((response & 0x80000000U) == 0U) {
		volt_retry++;
		if (volt_retry > 10000)
			return;

		sdcard_timer_handle = Timer_Oneshot
		(10000, sigc::ptr_fun(&SDcard_init_operation_condition_timer));

		return;
	}

	if (response & SD_HIGH_CAPACITY) {
		// hc card
		card_type = CardHC;
	}

	//card is now in READY state

	SDcard_init_get_CID();
}

static void SDcard_init_operation_condition_timer() {
	SDcard_init_operation_condition();
}

static void SDcard_init_get_CID() {
	//Cmd2: get CID (from all connected cards)
	command.argument = 0;
	command.command = 2;
	command.responseType = SDResponseType::ResponseLong;
	command.dataType = SDDataType::NoData;
	command.retryCounter = 2;
	command.data = NULL;
	command.slot = sigc::ptr_fun(&SDcard_init_get_CID_cmpl);

	SDIO_Command(&command);
}

static void SDcard_init_get_CID_cmpl(int result) {
	if (result != SDIO_OK)
		return;

	card_CID.d[3] = command.response[0];
	card_CID.d[2] = command.response[1];
	card_CID.d[1] = command.response[2];
	card_CID.d[0] = command.response[3];

	//card is now in IDENT state

	SDcard_init_set_addr();
}

static void SDcard_init_set_addr() {
	//cmd3: set relative address
	//(card generates a new address for itself)
	command.argument = 0;
	command.command = 3;
	command.responseType = SDResponseType::ResponseShort;
	command.dataType = SDDataType::NoData;
	command.retryCounter = 2;
	command.data = NULL;
	command.slot = sigc::ptr_fun(&SDcard_init_set_addr_cmpl);

	SDIO_Command(&command);
}

static void SDcard_init_set_addr_cmpl(int result) {
	if (result != SDIO_OK)
		return;

	uint32_t response_r1 = command.response[0];

	if (response_r1 &(SD_R6_GENERAL_UNKNOWN_ERROR | SD_R6_ILLEGAL_CMD | SD_R6_COM_CRC_FAILED)) {
		return;
	}

	card_rca = response_r1 >> 16;

	//card is now in STBY state

	SDcard_init_get_CSD();
}

static void SDcard_init_get_CSD() {
	//Cmd9: query CSD
	command.argument = card_rca << 16;
	command.command = 9;
	command.responseType = SDResponseType::ResponseLong;
	command.dataType = SDDataType::NoData;
	command.retryCounter = 2;
	command.data = NULL;
	command.slot = sigc::ptr_fun(&SDcard_init_get_CSD_cmpl);

	SDIO_Command(&command);
}

static void SDcard_init_get_CSD_cmpl(int result) {
	if (result != SDIO_OK)
		return;

	card_CSD.d[3] = command.response[0];
	card_CSD.d[2] = command.response[1];
	card_CSD.d[1] = command.response[2];
	card_CSD.d[0] = command.response[3];

	if ((card_CSD.v2.CSD_structure == 0 && card_type == CardHC) ||
	    (card_CSD.v1.CSD_structure == 1 && card_type != CardHC) ||
	    card_CSD.v2.CSD_structure > 1) {
		return;
	}

	if (card_CSD.v1.CSD_structure == 0)
		card_khz = card_CSD.v1.max_data_transfer_rate;
	if (card_CSD.v2.CSD_structure == 1)
		card_khz = card_CSD.v1.max_data_transfer_rate;
	static int Clock_Units[8] = {10, 100, 1000, 10000, 0, 0, 0, 0};
	static int Clock_Mults[16] = { 0, 10, 12, 13, 15, 20, 25, 30,
				       35, 40, 45, 50, 55, 60, 70, 80 };
	card_khz = Clock_Units[(card_khz & 0x7)] *
		Clock_Mults[((card_khz >> 3) & 0xf)];

	//card is still in STBY state

	SDcard_init_select_card();
}

static void SDcard_init_select_card() {
	/* Cmd7: Select card */
	command.argument = card_rca << 16;
	command.command = 7;
	command.responseType = SDResponseType::Response1;
	command.dataType = SDDataType::NoData;
	command.retryCounter = 2;
	command.data = NULL;
	command.slot = sigc::ptr_fun(&SDcard_init_select_card_cmpl);

	SDIO_Command(&command);
}

static void SDcard_init_select_card_cmpl(int result) {
	if (result != SDIO_OK)
		return;

	//card is now in TRAN state

	SDcard_init_get_SCR();
}

static void SDcard_init_get_status() {
	command.argument = card_rca << 16;
	command.command = 13;
	command.responseType = SDResponseType::Response1;
	command.dataType = SDDataType::NoData;
	command.retryCounter = 2;
	command.data = NULL;
	command.slot = sigc::ptr_fun(&SDcard_init_get_status_cmpl);

	SDIO_Command(&command);
}

static void SDcard_init_get_status_cmpl(int result) {
	if (result != SDIO_OK) {
		return;
	}

	__BKPT(1);

	SDcard_init_get_SCR();
}

static void SDcard_init_get_SCR() {
	/* ACMD51: Send SCR */
	command.argument = 0;
	command.command = 51 | SDIO_APPCMD;
	command.rca = card_rca;
	command.responseType = SDResponseType::Response1;
	command.dataType = SDDataType::DataToSDIO;
	command.retryCounter = 2;
	command.data = &card_SCR.d[0];
	command.datalength = 8;
	command.datablocksize = SDIO_DataBlockSize_8b;
	command.slot = sigc::ptr_fun(&SDcard_init_get_SCR_cmpl);

	SDIO_Command(&command);
}

static void SDcard_init_get_SCR_cmpl(int result) {
	if (result != SDIO_OK)
		return;

	for(int i = 0; i < 3; i++) {
		uint8_t t = card_SCR.d[i];
		card_SCR.d[i] = card_SCR.d[7-i];
		card_SCR.d[7-i] = t;
	}

	SDcard_init_set_blocksize();
}

static void SDcard_init_set_blocksize() {
	/* Cmd16: Set Block Size for Card */
	command.argument = 512;
	command.command = 16;
	command.responseType = SDResponseType::Response1;
	command.dataType = SDDataType::NoData;
	command.retryCounter = 2;
	command.data = NULL;
	command.slot = sigc::ptr_fun(&SDcard_init_set_blocksize_cmpl);

	SDIO_Command(&command);
}

static void SDcard_init_set_blocksize_cmpl(int result) {
	if (result != SDIO_OK)
		return;

	SDcard_init_set_bus_width();
}

static void SDcard_init_set_bus_width() {
	if(!(card_SCR.v1.sd_bus_widths & 0x4)) {
		//card does not support wide bus.
		SDIO_ConfigureBus(0, card_khz);

		SDcard_init_finish();
	}

	command.argument = 0x2; //four bits
	command.command = 6 | SDIO_APPCMD;
	command.rca = card_rca;
	command.responseType = SDResponseType::Response1;//no crc
	command.dataType = SDDataType::NoData;
	command.retryCounter = 2;
	command.data = NULL;
	command.slot = sigc::ptr_fun(&SDcard_init_set_bus_width_cmpl);

	SDIO_Command(&command);
}

static void SDcard_init_set_bus_width_cmpl(int result) {
	if (result != SDIO_OK)
		return;

	SDIO_ConfigureBus(1, card_khz);

	SDcard_init_finish();
}

static void SDcard_init_finish() {
	//card init done.
	if (card_CSD.v1.CSD_structure == 0) {
		//v1.0
		sdcard.size = (card_CSD.v1.device_size+1)*(4 << card_CSD.v1.device_size_multiplier)*(1 << card_CSD.v1.max_read_data_block_length);
	} else if (card_CSD.v1.CSD_structure == 1) {
		sdcard.size = (card_CSD.v2.device_size+1)*512;
	} else {
		return; //hmm. this card appears to be newer than we support.
	}
	MSD_Register(&sdcard);
}

static void SDcard_deinit() {
	//switch pins back to input only
	GPIO_SetBits(SD_PWR_GPIO, SD_PWR_PIN);//switch it off.

	SDIO_PowerDown();

	MSD_Unregister(&sdcard);
}

void EXTI9_5_IRQHandler() {
	if (EXTI_GetITStatus(SD_CD_EXTI_Line) == SET) {
		EXTI_ClearFlag(SD_CD_EXTI_Line);
		EXTI_ClearITPendingBit(SD_CD_EXTI_Line);
		if (!card_present) {
			if (GPIO_ReadInputDataBit(SD_CD_GPIO, SD_CD_PIN) == Bit_SET)
				return;

			card_present = 1;

			EXTI_InitTypeDef extiinit;
			EXTI_StructInit(&extiinit);
			extiinit.EXTI_Line = SD_CD_EXTI_Line;
			extiinit.EXTI_Mode = EXTI_Mode_Interrupt;
			extiinit.EXTI_Trigger = EXTI_Trigger_Rising;
			extiinit.EXTI_LineCmd = ENABLE;
			EXTI_Init(&extiinit);

			//init the card here.
			sdcard_timer_handle = Timer_Oneshot
			(100, sigc::ptr_fun(&SDcard_init_power));
			//notify anyone needing to know the card appeared.

			if (GPIO_ReadInputDataBit(SD_CD_GPIO, SD_CD_PIN) == Bit_SET) {
				//card disappeared. make sure we get the irq.
				EXTI_GenerateSWInterrupt(SD_CD_EXTI_Line);
			}
		} else {
			if (GPIO_ReadInputDataBit(SD_CD_GPIO, SD_CD_PIN) == Bit_RESET)
				return;

			card_present = 0;

			if (sdcard_timer_handle) {
				Timer_Cancel(sdcard_timer_handle);
				sdcard_timer_handle = 0;
			}

			EXTI_InitTypeDef extiinit;
			EXTI_StructInit(&extiinit);
			extiinit.EXTI_Line = SD_CD_EXTI_Line;
			extiinit.EXTI_Mode = EXTI_Mode_Interrupt;
			extiinit.EXTI_Trigger = EXTI_Trigger_Rising;
			extiinit.EXTI_LineCmd = ENABLE;
			EXTI_Init(&extiinit);

			SDcard_deinit();
			//notify anyone needing to know the card disappeared.

			if (GPIO_ReadInputDataBit(SD_CD_GPIO, SD_CD_PIN) == Bit_RESET) {
				//card reappeared. make sure we get the irq.
				EXTI_GenerateSWInterrupt(SD_CD_EXTI_Line);
			}
		}
	}
}

static void SDcard_read_sectors2(struct MSDReadCommand *command);
static void SDcard_read_sectors2_cmpl(int result, struct MSDReadCommand *command);
static void SDcard_read_sectors3(struct MSDReadCommand *command);
static void SDcard_read_sectors3_cmpl(int result, struct MSDReadCommand *command);
static void SDcard_write_sectors2(struct MSDWriteCommand *command);

struct SDcard_rwCommand{
  MSDReadCommand *readcmd;
  MSDWriteCommand *writecmd;
};

static SDcard_rwCommand currentCommand = {NULL, NULL};
static std::deque<SDcard_rwCommand> commandQueue;

static void SDcard_dequeueNextCommand() {
	ISR_Guard g;
	if (commandQueue.empty()) {
		currentCommand.readcmd = NULL;
		currentCommand.writecmd = NULL;
		return;
	}
	currentCommand = commandQueue.front();
	commandQueue.pop_front();
	if (currentCommand.readcmd) {
		SDcard_read_sectors2(currentCommand.readcmd);
	}
	if (currentCommand.writecmd) {
		SDcard_write_sectors2(currentCommand.writecmd);
	}
}

void SDCard::readBlocks(struct MSDReadCommand *command) {
	ISR_Guard g;
	if (currentCommand.readcmd || currentCommand.writecmd) {
		SDcard_rwCommand c = {command , NULL};
		commandQueue.push_back(c);
	} else {
		currentCommand.readcmd = command;
		SDcard_read_sectors2(command);
	}
}

static void SDcard_read_sectors2(struct MSDReadCommand *command) {
	if (card_type == CardHC) {
		command->sdcard.sdcommand.argument = command->start_block;
	} else {
		command->sdcard.sdcommand.argument = command->start_block * 512;
	}
	if (command->num_blocks > 1) {
		/* Send CMD18 READ_MULT_BLOCK */
		command->sdcard.sdcommand.command = 18;
	} else {
		/* Send CMD17 READ_SINGLE_BLOCK */
		command->sdcard.sdcommand.command = 17;
	}
	command->sdcard.sdcommand.rca = card_rca;
	command->sdcard.sdcommand.responseType = SDResponseType::Response1;
	command->sdcard.sdcommand.retryCounter = 2;
	command->sdcard.sdcommand.data = command->dst;
	command->sdcard.sdcommand.datalength = 512*command->num_blocks;
	command->sdcard.sdcommand.datablocksize = SDIO_DataBlockSize_512b;
	command->sdcard.sdcommand.dataType = SDDataType::DataToSDIO;
	command->sdcard.sdcommand.slot = sigc::bind(sigc::ptr_fun(&SDcard_read_sectors2_cmpl), command);

	SDIO_Command(&command->sdcard.sdcommand);
}

static void SDcard_read_sectors2_cmpl(int result, struct MSDReadCommand *command) {
	if (result != SDIO_OK) {
		if (result == SDIO_CommandTimeout)
			command->slot(ETIMEDOUT);
		else if (result == SDIO_CSError) {
			if (command->sdcard.sdcommand.response[0] &
			    SD_CS_ADDR_OUT_OF_RANGE)
				command->slot(ENODATA); //for write, ENOSPC
			else
				command->slot(EIO);
		} else {
			command->slot(EIO);
		}
		SDcard_dequeueNextCommand();
		return;
	}

	if (command->num_blocks > 1) {
		SDcard_read_sectors3(command);

		return;
	}

	command->slot(0);
	SDcard_dequeueNextCommand();
}

static void SDcard_read_sectors3(struct MSDReadCommand *command) {
	/* Cmd12: STOP_TRANSMISSION */
	command->sdcard.sdcommand.argument = 0;
	command->sdcard.sdcommand.command = 12;
	command->sdcard.sdcommand.responseType = SDResponseType::Response1;
	command->sdcard.sdcommand.dataType = SDDataType::NoData;
	command->sdcard.sdcommand.retryCounter = 2;
	command->sdcard.sdcommand.data = NULL;
	command->sdcard.sdcommand.slot = sigc::bind(sigc::ptr_fun(&SDcard_read_sectors3_cmpl), command);

	SDIO_Command(&command->sdcard.sdcommand);
}

static void SDcard_read_sectors3_cmpl(int result, struct MSDReadCommand *command) {
	if (result != SDIO_OK) {
		if (result == SDIO_CommandTimeout)
			command->slot(ETIMEDOUT);
		else
			command->slot(EIO);
		SDcard_dequeueNextCommand();
		return;
	}

	command->slot(0);
	SDcard_dequeueNextCommand();
}

void SDCard::writeBlocks(struct MSDWriteCommand *command) {
	ISR_Guard g;
	if (currentCommand.readcmd || currentCommand.writecmd) {
		SDcard_rwCommand c = { NULL, command };
		commandQueue.push_back(c);
	} else {
		currentCommand.writecmd = command;
		SDcard_write_sectors2(command);
	}
}

static void SDcard_write_sectors2(struct MSDWriteCommand *command) {
	command->slot(-1);
	SDcard_dequeueNextCommand();
}
