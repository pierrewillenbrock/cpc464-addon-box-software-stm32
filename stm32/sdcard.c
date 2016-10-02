
#include <stdlib.h>
#include <errno.h>

#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_exti.h"
#include "stm32f4xx_syscfg.h"
#include "irq.h"
#include "timer.h"
#include "msd.h"
#include "sdio.h"

//hardware definitions

#define SD_PWR_PIN GPIO_Pin_8
#define SD_PWR_GPIO GPIOA
#define SD_CD_PIN GPIO_Pin_5
#define SD_CD_GPIO GPIOB
#define SD_CD_EXTI_Line EXTI_Line5
#define SD_CD_EXTI_PortSourceGPIO EXTI_PortSourceGPIOB
#define SD_CD_EXTI_PinSource EXTI_PinSource5
#define SD_PINS1 (GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 | GPIO_Pin_12)
#define SD_GPIO1 GPIOC
#define SD_PINS2 (GPIO_Pin_2)
#define SD_GPIO2 GPIOD

#define SD_GPIO_RCC (RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_GPIOB | RCC_AHB1Periph_GPIOC | RCC_AHB1Periph_GPIOD)



// SD standard defintions

#define SD_R6_GENERAL_UNKNOWN_ERROR     ((uint32_t)0x00002000U)
#define SD_R6_ILLEGAL_CMD               ((uint32_t)0x00004000U)
#define SD_R6_COM_CRC_FAILED            ((uint32_t)0x00008000U)

//voltage window is 3.2-3.3. could try some different values as well.
//3.2-3.4(for +/-5%) is one option, could also try 3.0-3.6 (for +/-10%)
//would be 0x00300000 resp 0x00780000
#define SD_VOLTAGE_WINDOW_SD            ((uint32_t)0x00100000U)
#define SD_HIGH_CAPACITY                ((uint32_t)0x40000000U)

#define SD_CS_ADDR_OUT_OF_RANGE        ((uint32_t)0x80000000U)
#define SD_CS_ADDR_MISALIGNED          ((uint32_t)0x40000000U)
#define SD_CS_BLOCK_LEN_ERR            ((uint32_t)0x20000000U)
#define SD_CS_ERASE_SEQ_ERR            ((uint32_t)0x10000000U)
#define SD_CS_BAD_ERASE_PARAM          ((uint32_t)0x08000000U)
#define SD_CS_WRITE_PROT_VIOLATION     ((uint32_t)0x04000000U)
#define SD_CS_LOCK_UNLOCK_FAILED       ((uint32_t)0x01000000U)
#define SD_CS_COM_CRC_FAILED           ((uint32_t)0x00800000U)
#define SD_CS_ILLEGAL_CMD              ((uint32_t)0x00400000U)
#define SD_CS_CARD_ECC_FAILED          ((uint32_t)0x00200000U)
#define SD_CS_CC_ERROR                 ((uint32_t)0x00100000U)
#define SD_CS_GENERAL_UNKNOWN_ERROR    ((uint32_t)0x00080000U)
#define SD_CS_STREAM_READ_UNDERRUN     ((uint32_t)0x00040000U)
#define SD_CS_STREAM_WRITE_OVERRUN     ((uint32_t)0x00020000U)
#define SD_CS_CID_CSD_OVERWRITE        ((uint32_t)0x00010000U)
#define SD_CS_WP_ERASE_SKIP            ((uint32_t)0x00008000U)
#define SD_CS_CARD_ECC_DISABLED        ((uint32_t)0x00004000U)
#define SD_CS_ERASE_RESET              ((uint32_t)0x00002000U)
#define SD_CS_AKE_SEQ_ERROR            ((uint32_t)0x00000008U)
#define SD_CS_ERRORBITS                ((uint32_t)0xFDFFE008U)

int card_present = 0;
static uint32_t timer_handle = 0;
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

	RCC_AHB1PeriphClockCmd(SD_GPIO_RCC, ENABLE);

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

static void SDcard_readSectors(void *unused, struct MSDReadCommand *command);
static void SDcard_writeSectors(void *unused, struct MSDWriteCommand *command);

static struct MSD_Info sdcard_info = {
	0, 512, NULL, SDcard_readSectors, SDcard_writeSectors
};

static void SDcard_init_power(void *unused);
static void SDcard_init_clock(void *unused);
static void SDcard_init_reset();
static void SDcard_init_reset_cmpl(int result, struct SDCommand *unused);
static void SDcard_init_interface_condition();
static void SDcard_init_interface_condition_cmpl(int result, struct SDCommand *unused);
static void SDcard_init_sdcard_probe();
static void SDcard_init_sdcard_probe_cmpl(int result, struct SDCommand *unused);
static void SDcard_init_operation_condition();
static void SDcard_init_operation_condition_cmpl(int result, struct SDCommand *unused);
static void SDcard_init_operation_condition_timer(void *unused);
static void SDcard_init_get_CID();
static void SDcard_init_get_CID_cmpl(int result, struct SDCommand *unused);
static void SDcard_init_set_addr();
static void SDcard_init_set_addr_cmpl(int result, struct SDCommand *unused);
static void SDcard_init_get_CSD();
static void SDcard_init_get_CSD_cmpl(int result, struct SDCommand *unused);
static void SDcard_init_select_card();
static void SDcard_init_select_card_cmpl(int result, struct SDCommand *unused);
static void SDcard_init_get_SCR();
static void SDcard_init_get_SCR_cmpl(int result, struct SDCommand *unused);
static void SDcard_init_set_blocksize();
static void SDcard_init_set_blocksize_cmpl(int result, struct SDCommand *unused);
static void SDcard_init_set_bus_width();
static void SDcard_init_set_bus_width_cmpl(int result, struct SDCommand *unused);
static void SDcard_init_finish();

static void SDcard_init_get_status();
static void SDcard_init_get_status_cmpl(int result, struct SDCommand *unused);

static void SDcard_init_power(void *unused) {
	//irq context: cannot wait here for long. need to drive it using irqs.
	//switch pins back to AF
	GPIO_ResetBits(SD_PWR_GPIO, SD_PWR_PIN);//switch it on

	SDIO_PowerUp();

	timer_handle = Timer_Oneshot(10000, SDcard_init_clock, NULL);
}

static void SDcard_init_clock(void *unused) {
	//irq context: cannot wait here for long. need to drive it using irqs.
	SDIO_ClockEnable();

	SDcard_init_reset();
}

static struct SDCommand command;

static void SDcard_init_reset() {
	command.argument = 0;
	command.command = 0;
	command.responseType = NoResponse;
	command.dataType = NoData;
	command.retryCounter = 0;
	command.data = NULL;
	command.completion = SDcard_init_reset_cmpl;

	SDIO_Command(&command);
}

static void SDcard_init_reset_cmpl(int result, struct SDCommand *unused) {
	if (result == SDIO_SystemTimeout)
		return;

	//card is in IDLE state

	SDcard_init_interface_condition();
}

static void SDcard_init_interface_condition() {
	command.argument = 0x000001aa;
	command.command = 8;
	command.responseType = ResponseShort;
	command.dataType = NoData;
	command.retryCounter = 2;
	command.data = NULL;
	command.completion = SDcard_init_interface_condition_cmpl;

	SDIO_Command(&command);
}

static void SDcard_init_interface_condition_cmpl(int result, struct SDCommand *unused) {
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
	command.responseType = Response1;
	command.dataType = NoData;
	command.retryCounter = 2;
	command.data = NULL;
	command.completion = SDcard_init_sdcard_probe_cmpl;

	SDIO_Command(&command);
}

static void SDcard_init_sdcard_probe_cmpl(int result, struct SDCommand *unused) {
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
	command.responseType = Response3;//no crc
	command.dataType = NoData;
	command.retryCounter = 2;
	command.data = NULL;
	command.completion = SDcard_init_operation_condition_cmpl;

	SDIO_Command(&command);
}

static void SDcard_init_operation_condition_cmpl(int result, struct SDCommand *unused) {
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

		timer_handle = Timer_Oneshot(10000, SDcard_init_operation_condition_timer, NULL);
		return;
	}

	uint32_t response = command.response[0];

	if ((response & 0x80000000U) == 0U) {
		volt_retry++;
		if (volt_retry > 10000)
			return;

		timer_handle = Timer_Oneshot(10000, SDcard_init_operation_condition_timer, NULL);

		return;
	}

	if (response & SD_HIGH_CAPACITY) {
		// hc card
		card_type = CardHC;
	}

	//card is now in READY state

	SDcard_init_get_CID();
}

static void SDcard_init_operation_condition_timer(void *unused) {
	SDcard_init_operation_condition();
}

static void SDcard_init_get_CID() {
	//Cmd2: get CID (from all connected cards)
	command.argument = 0;
	command.command = 2;
	command.responseType = ResponseLong;
	command.dataType = NoData;
	command.retryCounter = 2;
	command.data = NULL;
	command.completion = SDcard_init_get_CID_cmpl;

	SDIO_Command(&command);
}

static void SDcard_init_get_CID_cmpl(int result, struct SDCommand *unused) {
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
	command.responseType = ResponseShort;
	command.dataType = NoData;
	command.retryCounter = 2;
	command.data = NULL;
	command.completion = SDcard_init_set_addr_cmpl;

	SDIO_Command(&command);
}

static void SDcard_init_set_addr_cmpl(int result, struct SDCommand *unused) {
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
	command.responseType = ResponseLong;
	command.dataType = NoData;
	command.retryCounter = 2;
	command.data = NULL;
	command.completion = SDcard_init_get_CSD_cmpl;

	SDIO_Command(&command);
}

static void SDcard_init_get_CSD_cmpl(int result, struct SDCommand *unused) {
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
	command.responseType = Response1;
	command.dataType = NoData;
	command.retryCounter = 2;
	command.data = NULL;
	command.completion = SDcard_init_select_card_cmpl;

	SDIO_Command(&command);
}

static void SDcard_init_select_card_cmpl(int result, struct SDCommand *unused) {
	if (result != SDIO_OK)
		return;

	//card is now in TRAN state

	SDcard_init_get_SCR();
}

static void SDcard_init_get_status() {
	command.argument = card_rca << 16;
	command.command = 13;
	command.responseType = Response1;
	command.dataType = NoData;
	command.retryCounter = 2;
	command.data = NULL;
	command.completion = SDcard_init_get_status_cmpl;

	SDIO_Command(&command);
}

static void SDcard_init_get_status_cmpl(int result, struct SDCommand *unused) {
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
	command.responseType = Response1;
	command.dataType = DataToSDIO;
	command.retryCounter = 2;
	command.data = &card_SCR.d[0];
	command.datalength = 8;
	command.datablocksize = SDIO_DataBlockSize_8b;
	command.completion = SDcard_init_get_SCR_cmpl;

	SDIO_Command(&command);
}

static void SDcard_init_get_SCR_cmpl(int result, struct SDCommand *unused) {
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
	command.responseType = Response1;
	command.dataType = NoData;
	command.retryCounter = 2;
	command.data = NULL;
	command.completion = SDcard_init_set_blocksize_cmpl;

	SDIO_Command(&command);
}

static void SDcard_init_set_blocksize_cmpl(int result, struct SDCommand *unused) {
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
	command.responseType = Response1;//no crc
	command.dataType = NoData;
	command.retryCounter = 2;
	command.data = NULL;
	command.completion = SDcard_init_set_bus_width_cmpl;

	SDIO_Command(&command);
}

static void SDcard_init_set_bus_width_cmpl(int result, struct SDCommand *unused) {
	if (result != SDIO_OK)
		return;

	SDIO_ConfigureBus(1, card_khz);

	SDcard_init_finish();
}

static void SDcard_init_finish() {
	//card init done.
	if (card_CSD.v1.CSD_structure == 0) {
		//v1.0
		sdcard_info.size = (card_CSD.v1.device_size+1)*(4 << card_CSD.v1.device_size_multiplier)*(1 << card_CSD.v1.max_read_data_block_length);
	} else if (card_CSD.v1.CSD_structure == 1) {
		sdcard_info.size = (card_CSD.v2.device_size+1)*512;
	} else {
		return; //hmm. this card appears to be newer than we support.
	}
	MSD_Register(&sdcard_info);
}

static void SDcard_deinit() {
	//switch pins back to input only
	GPIO_SetBits(SD_PWR_GPIO, SD_PWR_PIN);//switch it off.

	SDIO_PowerDown();

	MSD_Unregister(&sdcard_info);
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
			timer_handle = Timer_Oneshot(100, SDcard_init_power,
						     NULL);
			//notify anyone needing to know the card appeared.

			if (GPIO_ReadInputDataBit(SD_CD_GPIO, SD_CD_PIN) == Bit_SET) {
				//card disappeared. make sure we get the irq.
				EXTI_GenerateSWInterrupt(SD_CD_EXTI_Line);
			}
		} else {
			if (GPIO_ReadInputDataBit(SD_CD_GPIO, SD_CD_PIN) == Bit_RESET)
				return;

			card_present = 0;

			if (timer_handle) {
				Timer_Cancel(timer_handle);
				timer_handle = 0;
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
static void SDcard_read_sectors2_cmpl(int result, struct SDCommand *sdcommand);
static void SDcard_read_sectors3(struct MSDReadCommand *command);
static void SDcard_read_sectors3_cmpl(int result, struct SDCommand *sdcommand);

static void SDcard_readSectors(void *unused, struct MSDReadCommand *command) {
	SDcard_read_sectors2(command);
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
	command->sdcard.sdcommand.responseType = Response1;
	command->sdcard.sdcommand.retryCounter = 2;
	command->sdcard.sdcommand.data = command->dst;
	command->sdcard.sdcommand.datalength = 512*command->num_blocks;
	command->sdcard.sdcommand.datablocksize = SDIO_DataBlockSize_512b;
	command->sdcard.sdcommand.dataType = DataToSDIO;
	command->sdcard.sdcommand.completion = SDcard_read_sectors2_cmpl;

	SDIO_Command(&command->sdcard.sdcommand);
}

static void SDcard_read_sectors2_cmpl(int result, struct SDCommand *sdcommand) {
	struct MSDReadCommand *command = container_of(sdcommand, struct MSDReadCommand, sdcard.sdcommand);
	if (result != SDIO_OK) {
		if (result == SDIO_CommandTimeout)
			command->completion(ETIMEDOUT, command);
		else if (result == SDIO_CSError) {
			if (command->sdcard.sdcommand.response[0] &
			    SD_CS_ADDR_OUT_OF_RANGE)
				command->completion(ENODATA, command); //for write, ENOSPC
			else
				command->completion(EIO, command);
		} else {
			command->completion(EIO, command);
		}
		return;
	}

	if (command->num_blocks > 1) {
		SDcard_read_sectors3(command);

		return;
	}

	command->completion(0, command);
}

static void SDcard_read_sectors3(struct MSDReadCommand *command) {
	/* Cmd12: STOP_TRANSMISSION */
	command->sdcard.sdcommand.argument = 0;
	command->sdcard.sdcommand.command = 12;
	command->sdcard.sdcommand.responseType = Response1;
	command->sdcard.sdcommand.dataType = NoData;
	command->sdcard.sdcommand.retryCounter = 2;
	command->sdcard.sdcommand.data = NULL;
	command->sdcard.sdcommand.completion = SDcard_read_sectors3_cmpl;

	SDIO_Command(&command->sdcard.sdcommand);
}

static void SDcard_read_sectors3_cmpl(int result, struct SDCommand *sdcommand) {
	struct MSDReadCommand *command = container_of(sdcommand, struct MSDReadCommand, sdcard.sdcommand);
	if (result != SDIO_OK) {
		if (result == SDIO_CommandTimeout)
			command->completion(ETIMEDOUT, command);
		else
			command->completion(EIO, command);
		return;
	}

	command->completion(0, command);
}

static void SDcard_writeSectors(void *unused, struct MSDWriteCommand *command) {
	command->completion(-1, command);
}

