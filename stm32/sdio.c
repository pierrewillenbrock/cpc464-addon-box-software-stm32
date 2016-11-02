
#include "sdio.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "stm32f4xx_gpio.h"
#include "stm32f4xx_sdio.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_syscfg.h"
#include "stm32f4xx_dma.h"
#include "irq.h"
#include "timer.h"

//hardware definitions

#define SD_CLK_PIN GPIO_Pin_12
#define SD_CLK_GPIO GPIOC
#define SD_PINS1 (GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11)
#define SD_GPIO1 GPIOC
#define SD_PINS2 (GPIO_Pin_2)
#define SD_GPIO2 GPIOD

#define SD_GPIO_RCC (RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_GPIOB | RCC_AHB1Periph_GPIOC | RCC_AHB1Periph_GPIOD)

// SD standard defintions
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
//#define SD_CS_ERRORBITS                ((uint32_t)0xFDFFE008U)
#define SD_CS_ERRORBITS                ((uint32_t)0xFD7FE008U)


static uint32_t timer_handle = 0;
static struct SDCommand * volatile current_command = NULL;

#ifdef SDIO_DEBUG
struct SDDebug {
	struct SDCommand command;
	int result;
	uint32_t STA;
};

#define SD_DEBUG_COUNT 64
struct SDDebug sd_debug[SD_DEBUG_COUNT];
int sd_nextdebug = 0;

#define SD_DEBUG_SAMPLE(res, cmdp) do {		\
	memcpy(&sd_debug[sd_nextdebug].command,	\
	       (cmdp), sizeof(*(cmdp)));	\
	sd_debug[sd_nextdebug].result = (res);	\
	sd_debug[sd_nextdebug].STA = SDIO->STA;	\
	sd_nextdebug++;				\
	sd_nextdebug &= SD_DEBUG_COUNT-1;	\
	while(0)
#else
#define SD_DEBUG_SAMPLE(res, cmdp) (void)0
#endif

void SDIO_Setup() {
	RCC_AHB1PeriphClockCmd(SD_GPIO_RCC, ENABLE);

	//for now, keep the pins in IN, later switch to AF when we start to
	//use the SDIO component. Don't want to blow up the drivers
	//while inserting crap.
	GPIO_InitTypeDef gpio_init;
	GPIO_StructInit(&gpio_init);

	gpio_init.GPIO_Pin = SD_PINS1;
	gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_IN;
	GPIO_Init(SD_GPIO1, &gpio_init);
	gpio_init.GPIO_Pin = SD_PINS2;
	gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_IN;
	GPIO_Init(SD_GPIO2, &gpio_init);
	gpio_init.GPIO_Pin = SD_CLK_PIN;
	gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_IN;
	GPIO_Init(SD_CLK_GPIO, &gpio_init);
	GPIO_PinAFConfig(SD_GPIO1, GPIO_PinSource8, GPIO_AF_SDIO);
	GPIO_PinAFConfig(SD_GPIO1, GPIO_PinSource9, GPIO_AF_SDIO);
	GPIO_PinAFConfig(SD_GPIO1, GPIO_PinSource10, GPIO_AF_SDIO);
	GPIO_PinAFConfig(SD_GPIO1, GPIO_PinSource11, GPIO_AF_SDIO);
	GPIO_PinAFConfig(SD_GPIO1, GPIO_PinSource12, GPIO_AF_SDIO);
	GPIO_PinAFConfig(SD_GPIO2, GPIO_PinSource2, GPIO_AF_SDIO);

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SDIO, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);
}

void SDIO_PowerUp() {
	//switch pins to AF
	GPIO_InitTypeDef gpio_init;
	GPIO_StructInit(&gpio_init);

	//we are using the internal pull ups instead of external ones.
	//CMD, D0-D3 need pull ups. it may be possible to remove the
	//pull ups during operation.
	gpio_init.GPIO_Pin = SD_PINS1;
	gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_AF;
	gpio_init.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(SD_GPIO1, &gpio_init);
	gpio_init.GPIO_Pin = SD_PINS2;
	gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_AF;
	gpio_init.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(SD_GPIO2, &gpio_init);
	gpio_init.GPIO_Pin = SD_CLK_PIN;
	gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_AF;
	gpio_init.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(SD_CLK_GPIO, &gpio_init);

	//1 bit bus, 400 kHz
	SDIO_ConfigureBus(0, 400);

	SDIO_ClockCmd(DISABLE);
	SDIO_SetPowerState(SDIO_PowerState_ON);

	NVIC_InitTypeDef nvicinit;
	nvicinit.NVIC_IRQChannel = SDIO_IRQn;
	nvicinit.NVIC_IRQChannelPreemptionPriority = 3;
	nvicinit.NVIC_IRQChannelSubPriority = 3;
	nvicinit.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&nvicinit);

	nvicinit.NVIC_IRQChannel = DMA2_Stream3_IRQn;
	nvicinit.NVIC_IRQChannelPreemptionPriority = 3;
	nvicinit.NVIC_IRQChannelSubPriority = 3;
	nvicinit.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&nvicinit);
}

void SDIO_ClockEnable() {
	SDIO_ClockCmd(ENABLE);
}

void SDIO_ConfigureBus(int widebus, int maxkhz) {
	SDIO_InitTypeDef sdioinit;
	SDIO_StructInit(&sdioinit);
	//defines relationship between internal clock and CK pin. probably okay?
	sdioinit.SDIO_ClockEdge = SDIO_ClockEdge_Rising;
	sdioinit.SDIO_ClockBypass = SDIO_ClockBypass_Disable;
	sdioinit.SDIO_ClockPowerSave = SDIO_ClockPowerSave_Disable;// not during setup, maybe later.
	sdioinit.SDIO_BusWide = widebus?SDIO_BusWide_4b:SDIO_BusWide_1b;
	sdioinit.SDIO_HardwareFlowControl = SDIO_HardwareFlowControl_Enable;
	int div = (48000+maxkhz-1)/maxkhz;//round up
	if (div > 253)
		div = 253;
	if (div < 2)
		div = 2;
	sdioinit.SDIO_ClockDiv = div-2; //48/(0+2) = 24. 48/(118+2) = 0.4.
	SDIO_Init(&sdioinit);
}

void SDIO_PowerDown() {
	GPIO_InitTypeDef gpio_init;
	GPIO_StructInit(&gpio_init);

	gpio_init.GPIO_Pin = SD_PINS1;
	gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_IN;
	GPIO_Init(SD_GPIO1, &gpio_init);
	gpio_init.GPIO_Pin = SD_PINS2;
	gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_IN;
	GPIO_Init(SD_GPIO2, &gpio_init);
	gpio_init.GPIO_Pin = SD_CLK_PIN;
	gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_IN;
	GPIO_Init(SD_CLK_GPIO, &gpio_init);

	SDIO_DeInit();
}

static void SDIO_timeout(void *unused);

void SDIO_IRQHandler(void) {
	if (!current_command)
		return;
	struct SDCommand *c = current_command;
	if (c->state == 0) {
		Timer_Cancel(timer_handle);
		timer_handle = 0;

		int result = SDIO_OK;

		if (SDIO_GetFlagStatus(SDIO_FLAG_CCRCFAIL))
			result = SDIO_CommandCRC;
		if (SDIO_GetFlagStatus(SDIO_FLAG_CTIMEOUT))
			result = SDIO_CommandTimeout;
		if (SDIO_GetFlagStatus(SDIO_FLAG_CMDREND)) {
			if (SDIO_GetCommandResponse() != 55)
				result = SDIO_CommandMismatch;

			c->response[0] = SDIO_GetResponse(SDIO_RESP1);

			if ((c->response[0] & SD_CS_ERRORBITS) != 0)
				result = SDIO_CSError;
		}

		//disable irqs
		SDIO_ITConfig(SDIO_IT_CMDREND, DISABLE);
		SDIO_ITConfig(SDIO_IT_CCRCFAIL, DISABLE);
		SDIO_ITConfig(SDIO_IT_CTIMEOUT, DISABLE);

		if (result != SDIO_OK && c->retryCounter > 0) {
			SD_DEBUG_SAMPLE(result, c);
			c->retryCounter--;
			current_command = NULL;
			SDIO_Command(c);
			return;
		}

		if (result != SDIO_OK) {
			//SDIO_ClearFlag(0x00c007ff);
			SD_DEBUG_SAMPLE(result, c);
			current_command = NULL;
			c->completion(result, c);
			return;
		}

		SD_DEBUG_SAMPLE(result, c);
		c->command &= ~SDIO_APPCMD;
		current_command = NULL;
		SDIO_Command(c);
		c->command |= SDIO_APPCMD;
	} else if (c->state == 1) {
		Timer_Cancel(timer_handle);
		timer_handle = 0;

		int result = SDIO_OK;

		//find out what the return status would be
		switch (c->responseType){
		case NoResponse:
			break;
		case ResponseShort:
			if (SDIO_GetFlagStatus(SDIO_FLAG_CCRCFAIL))
				result = SDIO_CommandCRC;
			if (SDIO_GetFlagStatus(SDIO_FLAG_CTIMEOUT))
				result = SDIO_CommandTimeout;
			if (SDIO_GetFlagStatus(SDIO_FLAG_CMDREND))
				c->response[0] = SDIO_GetResponse(SDIO_RESP1);
			break;
		case Response3: //CRC is not calculated for R3
			if (SDIO_GetFlagStatus(SDIO_FLAG_CTIMEOUT))
				result = SDIO_CommandTimeout;
			if (SDIO_GetFlagStatus(SDIO_FLAG_CMDREND) ||
			    SDIO_GetFlagStatus(SDIO_FLAG_CCRCFAIL))
				c->response[0] = SDIO_GetResponse(SDIO_RESP1);
			break;
		case Response1:
			if (SDIO_GetFlagStatus(SDIO_FLAG_CCRCFAIL))
				result = SDIO_CommandCRC;
			if (SDIO_GetFlagStatus(SDIO_FLAG_CTIMEOUT))
				result = SDIO_CommandTimeout;
			if (SDIO_GetFlagStatus(SDIO_FLAG_CMDREND)) {
				if (SDIO_GetCommandResponse() != (c->command & ~SDIO_APPCMD))
					result = SDIO_CommandMismatch;

				c->response[0] = SDIO_GetResponse(SDIO_RESP1);

				if ((c->response[0] & SD_CS_ERRORBITS) != 0)
					result = SDIO_CSError;
			}
			break;
		case ResponseLong:
			if (SDIO_GetFlagStatus(SDIO_FLAG_CCRCFAIL))
				result = SDIO_CommandCRC;
			if (SDIO_GetFlagStatus(SDIO_FLAG_CTIMEOUT))
				result = SDIO_CommandTimeout;
			if (SDIO_GetFlagStatus(SDIO_FLAG_CMDREND)) {
				c->response[0] = SDIO_GetResponse(SDIO_RESP1);
				c->response[1] = SDIO_GetResponse(SDIO_RESP2);
				c->response[2] = SDIO_GetResponse(SDIO_RESP3);
				c->response[3] = SDIO_GetResponse(SDIO_RESP4);
			}
			break;
		}

		//disable irqs
		switch (c->responseType){
		case NoResponse:
			SDIO_ITConfig(SDIO_IT_CMDSENT, DISABLE);
			break;
		case ResponseShort:
		case ResponseLong:
		case Response1:
		case Response3:
			SDIO_ITConfig(SDIO_IT_CMDREND, DISABLE);
			SDIO_ITConfig(SDIO_IT_CCRCFAIL, DISABLE);
			SDIO_ITConfig(SDIO_IT_CTIMEOUT, DISABLE);
			break;
		}

		if (result != SDIO_OK && c->retryCounter > 0) {
			//SDIO_ClearFlag(0x00c007ff);
			SD_DEBUG_SAMPLE(result, c);
			c->retryCounter--;
			current_command = NULL;
			SDIO_Command(c);
			return;
		}

		if (result != SDIO_OK) {
			//SDIO_ClearFlag(0x00c007ff);
			SD_DEBUG_SAMPLE(result, c);
			current_command = NULL;
			c->completion(result, c);
			return;
		}

		switch(c->dataType) {
		case NoData:
			//SDIO_ClearFlag(0x00c007ff);
			SD_DEBUG_SAMPLE(result, c);
			current_command = NULL;
			c->completion(result, c);
			break;
		case DataToSDIO:
			SD_DEBUG_SAMPLE(result, c);
			c->state = 2;
			//enable irqs for data transfer
			SDIO_ITConfig(SDIO_IT_RXOVERR, ENABLE);
			SDIO_ITConfig(SDIO_IT_DCRCFAIL, ENABLE);
			SDIO_ITConfig(SDIO_IT_DTIMEOUT, ENABLE);
			SDIO_ITConfig(SDIO_IT_STBITERR, ENABLE);
			timer_handle = Timer_Oneshot(1000000, SDIO_timeout, NULL);
			break;
		case DataToCard:
			SD_DEBUG_SAMPLE(result, c);
			c->state = 3;
			//enable irqs for data transfer
			SDIO_ITConfig(SDIO_IT_TXUNDERR, ENABLE);
			SDIO_ITConfig(SDIO_IT_DCRCFAIL, ENABLE);
			SDIO_ITConfig(SDIO_IT_DTIMEOUT, ENABLE);
			SDIO_ITConfig(SDIO_IT_STBITERR, ENABLE);
			timer_handle = Timer_Oneshot(1000, SDIO_timeout, NULL);
			break;
		}
	} else if (c->state == 2 || c->state == 3) {
		Timer_Cancel(timer_handle);
		timer_handle = 0;

		int result = SDIO_UnknownError;

		if (SDIO_GetFlagStatus(SDIO_FLAG_RXOVERR))
			result = SDIO_DataRxOverrun;
		if (SDIO_GetFlagStatus(SDIO_FLAG_DCRCFAIL))
			result = SDIO_DataCRC;
		if (SDIO_GetFlagStatus(SDIO_FLAG_DTIMEOUT))
			result = SDIO_DataTimeout;
		if (SDIO_GetFlagStatus(SDIO_FLAG_STBITERR))
			result = SDIO_DataStartBitError;

		SDIO_ITConfig(SDIO_IT_RXOVERR, DISABLE);
		SDIO_ITConfig(SDIO_IT_TXUNDERR, DISABLE);
		SDIO_ITConfig(SDIO_IT_DCRCFAIL, DISABLE);
		SDIO_ITConfig(SDIO_IT_DTIMEOUT, DISABLE);
		SDIO_ITConfig(SDIO_IT_STBITERR, DISABLE);
		//SDIO_ClearFlag(0x00c007ff);

		SD_DEBUG_SAMPLE(result, c);
		current_command = NULL;
		c->completion(result, c);
	}
}

void DMA2_Stream3_IRQHandler() {
	struct SDCommand *c = current_command;
	current_command = NULL;
	Timer_Cancel(timer_handle);
	timer_handle = 0;
	SDIO_ITConfig(SDIO_IT_RXOVERR, DISABLE);
	SDIO_ITConfig(SDIO_IT_DCRCFAIL, DISABLE);
	SDIO_ITConfig(SDIO_IT_DTIMEOUT, DISABLE);
	SDIO_ITConfig(SDIO_IT_STBITERR, DISABLE);
	switch (c->responseType){
	case NoResponse:
		SDIO_ITConfig(SDIO_IT_CMDSENT, DISABLE);
		break;
	case ResponseShort:
	case ResponseLong:
	case Response1:
	case Response3:
		SDIO_ITConfig(SDIO_IT_CMDREND, DISABLE);
		SDIO_ITConfig(SDIO_IT_CCRCFAIL, DISABLE);
		SDIO_ITConfig(SDIO_IT_CTIMEOUT, DISABLE);
		break;
	}
	DMA_ITConfig(DMA2_Stream3, DMA_IT_TE | DMA_IT_TC, DISABLE);
	if (DMA_GetFlagStatus(DMA2_Stream3, DMA_FLAG_TEIF3)) {
		DMA_ClearITPendingBit(DMA2_Stream3, DMA_IT_TEIF3);
		if (c->retryCounter > 0) {
			SD_DEBUG_SAMPLE(SDIO_SystemTimeout, c);
			c->retryCounter--;
			SDIO_Command(c);
			return;
		}
		//SDIO_ClearFlag(0x00c007ff);
		SD_DEBUG_SAMPLE(SDIO_DMAError, c);
		c->completion(SDIO_DMAError, c);
	}
	if (DMA_GetFlagStatus(DMA2_Stream3, DMA_FLAG_TCIF3)) {
		DMA_ClearITPendingBit(DMA2_Stream3, DMA_IT_TCIF3);
		SD_DEBUG_SAMPLE(0, c);
		c->completion(0, c);
	}
}

static void SDIO_timeout(void *unused) {
	struct SDCommand *c = current_command;
	current_command = NULL;
	timer_handle = 0;
	if (c->retryCounter > 0) {
		SD_DEBUG_SAMPLE(SDIO_SystemTimeout, c);
		c->retryCounter--;
		SDIO_Command(c);
		return;
	}
	switch (c->responseType){
	case NoResponse:
		SDIO_ITConfig(SDIO_IT_CMDSENT, DISABLE);
		break;
	case ResponseShort:
	case ResponseLong:
	case Response1:
	case Response3:
		SDIO_ITConfig(SDIO_IT_CMDREND, DISABLE);
		SDIO_ITConfig(SDIO_IT_CCRCFAIL, DISABLE);
		SDIO_ITConfig(SDIO_IT_CTIMEOUT, DISABLE);
		break;
	}
	SDIO_ITConfig(SDIO_IT_RXOVERR, DISABLE);
	SDIO_ITConfig(SDIO_IT_DCRCFAIL, DISABLE);
	SDIO_ITConfig(SDIO_IT_DTIMEOUT, DISABLE);
	SDIO_ITConfig(SDIO_IT_STBITERR, DISABLE);
	DMA_ITConfig(DMA2_Stream3, DMA_IT_TE | DMA_IT_TC, DISABLE);
	//SDIO_ClearFlag(0x00c007ff);
	SD_DEBUG_SAMPLE(SDIO_SystemTimeout, c);
	c->completion(SDIO_SystemTimeout, c);
}

void SDIO_Command(struct SDCommand *command) {
	assert(!current_command);

	SDIO_ClearFlag(0x00c007ff);

	SDIO_DataInitTypeDef sdiodatainit;
	SDIO_DataStructInit(&sdiodatainit);
	SDIO_CmdInitTypeDef sdiocmdinit;
	SDIO_CmdStructInit(&sdiocmdinit);
	DMA_InitTypeDef dmainit;
	DMA_StructInit(&dmainit);

	dmainit.DMA_Channel = DMA_Channel_4;
	dmainit.DMA_PeripheralBaseAddr = (uint32_t)&(SDIO->FIFO);
	dmainit.DMA_Memory0BaseAddr = (uint32_t)command->data;
	dmainit.DMA_BufferSize = command->datalength;
	dmainit.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	dmainit.DMA_MemoryInc = DMA_MemoryInc_Enable;
	dmainit.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
	dmainit.DMA_MemoryDataSize = DMA_MemoryDataSize_Word;
	dmainit.DMA_Mode = DMA_Mode_Normal;
	dmainit.DMA_Priority = DMA_Priority_Low;
	dmainit.DMA_FIFOMode = DMA_FIFOMode_Enable;
	dmainit.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
	dmainit.DMA_MemoryBurst = DMA_MemoryBurst_INC4;
	dmainit.DMA_PeripheralBurst = DMA_PeripheralBurst_INC4;

	if (command->command & SDIO_APPCMD) {
		sdiodatainit.SDIO_DataTimeOut = 0;
		DMA_ITConfig(DMA2_Stream3, DMA_IT_TE | DMA_IT_TC, DISABLE);
		sdiocmdinit.SDIO_Argument = command->rca << 16;
		sdiocmdinit.SDIO_CmdIndex = 55;
		sdiocmdinit.SDIO_Response = SDIO_Response_Short;
		SDIO_ITConfig(SDIO_IT_CMDREND, ENABLE);
		SDIO_ITConfig(SDIO_IT_CCRCFAIL, ENABLE);
		SDIO_ITConfig(SDIO_IT_CTIMEOUT, ENABLE);
		command->state = 0;
	} else {
		switch(command->dataType) {
		case NoData:
			sdiodatainit.SDIO_DataTimeOut = 0;
			DMA_ITConfig(DMA2_Stream3, DMA_IT_TE | DMA_IT_TC, DISABLE);
			break;
		case DataToSDIO:
			sdiodatainit.SDIO_DataLength = command->datalength;
			sdiodatainit.SDIO_DataBlockSize = command->datablocksize;
			sdiodatainit.SDIO_TransferDir = SDIO_TransferDir_ToSDIO;
			sdiodatainit.SDIO_TransferMode = SDIO_TransferMode_Block;
			sdiodatainit.SDIO_DPSM = SDIO_DPSM_Enable;
			SDIO_DMACmd(ENABLE);

			dmainit.DMA_DIR = DMA_DIR_PeripheralToMemory;
			DMA_Init(DMA2_Stream3, &dmainit);
			DMA_FlowControllerConfig(DMA2_Stream3, DMA_FlowCtrl_Peripheral);
			DMA_Cmd(DMA2_Stream3, ENABLE);
			DMA_ITConfig(DMA2_Stream3, DMA_IT_TE | DMA_IT_TC, ENABLE);
			break;
		case DataToCard:
			sdiodatainit.SDIO_DataLength = command->datalength;
			sdiodatainit.SDIO_DataBlockSize = command->datablocksize;
			sdiodatainit.SDIO_TransferDir = SDIO_TransferDir_ToCard;
			sdiodatainit.SDIO_TransferMode = SDIO_TransferMode_Block;
			sdiodatainit.SDIO_DPSM = SDIO_DPSM_Enable;
			SDIO_DMACmd(ENABLE);

			dmainit.DMA_DIR = DMA_DIR_MemoryToPeripheral;
			DMA_Init(DMA2_Stream3, &dmainit);
			DMA_FlowControllerConfig(DMA2_Stream3, DMA_FlowCtrl_Peripheral);
			DMA_Cmd(DMA2_Stream3, ENABLE);
			DMA_ITConfig(DMA2_Stream3, DMA_IT_TE | DMA_IT_TC, ENABLE);
			break;
		}

		sdiocmdinit.SDIO_Argument = command->argument;
		sdiocmdinit.SDIO_CmdIndex = command->command;
		switch (command->responseType){
		case NoResponse:
			sdiocmdinit.SDIO_Response = SDIO_Response_No;
			SDIO_ITConfig(SDIO_IT_CMDSENT, ENABLE);
			break;
		case ResponseShort:
		case Response1:
		case Response3:
			sdiocmdinit.SDIO_Response = SDIO_Response_Short;
			SDIO_ITConfig(SDIO_IT_CMDREND, ENABLE);
			SDIO_ITConfig(SDIO_IT_CCRCFAIL, ENABLE);
			SDIO_ITConfig(SDIO_IT_CTIMEOUT, ENABLE);
			break;
		case ResponseLong:
			sdiocmdinit.SDIO_Response = SDIO_Response_Long;
			SDIO_ITConfig(SDIO_IT_CMDREND, ENABLE);
			SDIO_ITConfig(SDIO_IT_CCRCFAIL, ENABLE);
			SDIO_ITConfig(SDIO_IT_CTIMEOUT, ENABLE);
			break;
		}
		command->state = 1;
	}

	sdiocmdinit.SDIO_Wait = SDIO_Wait_No;
	sdiocmdinit.SDIO_CPSM = SDIO_CPSM_Enable;

	current_command = command;
	timer_handle = Timer_Oneshot(10000, SDIO_timeout, NULL);
	command->datapos = 0;

	SDIO_DataConfig(&sdiodatainit);
	SDIO_SendCommand(&sdiocmdinit);
}

