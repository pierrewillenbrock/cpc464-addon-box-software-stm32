
#include <fpga/fpga_comm.hpp>
#include <fpga/layout.h>
#include <bsp/stm32f4xx_gpio.h>
#include <bsp/stm32f4xx_spi.h>
#include <bsp/stm32f4xx_rcc.h>
#include <bsp/stm32f4xx_dma.h>
#include <irq.h>
#include <bits.h>

#include <deque>
#include <assert.h>
#include <hw/fpga.h>

static FPGAComm_Command *fpga_current_command = NULL;
static std::deque<FPGAComm_Command *> workqueue;

void FPGAComm_Setup() {
	RCC_AHB1PeriphClockCmd(SPI_GPIO_RCC, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);

	GPIO_InitTypeDef gpio_init;
	GPIO_SetBits(SPI_NSS_GPIO, SPI_NSS_PIN);
	GPIO_StructInit(&gpio_init);
	gpio_init.GPIO_Pin = SPI_NSS_PIN;
	gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_Init(SPI_NSS_GPIO, &gpio_init);
	GPIO_ResetBits(SPI_NSS_GPIO, SPI_NSS_PIN);
	GPIO_SetBits(SPI_NSS_GPIO, SPI_NSS_PIN);

	gpio_init.GPIO_Pin = SPI_PINS;
	gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_AF;
	GPIO_Init(SPI_GPIO, &gpio_init);
	GPIO_PinAFConfig(SPI_GPIO, GPIO_PinSource5, SPI_AF);
	GPIO_PinAFConfig(SPI_GPIO, GPIO_PinSource6, SPI_AF);
	GPIO_PinAFConfig(SPI_GPIO, GPIO_PinSource7, SPI_AF);

	SPI_RCC_FUNC(SPI_RCC, ENABLE);

	/* sampling in rising edge, requiring a falling edge first to setup our
	   fpga output
	*/
	SPI_InitTypeDef spi_init;
	SPI_StructInit(&spi_init);
	spi_init.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
	spi_init.SPI_Mode = SPI_Mode_Master;
	spi_init.SPI_DataSize = SPI_DataSize_8b;
	spi_init.SPI_CPOL = SPI_CPOL_High;
	spi_init.SPI_CPHA = SPI_CPHA_2Edge;
	spi_init.SPI_NSS = SPI_NSS_Soft;
	//interestingly, even /8 works reliably
	spi_init.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_16;
	spi_init.SPI_FirstBit = SPI_FirstBit_LSB;
	SPI_Init(SPI_DEV, &spi_init);
	SPI_Cmd(SPI_DEV, ENABLE);


	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);

	gpio_init.GPIO_Pin = FPGA_IRQ_PIN;
	gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_IN;
	gpio_init.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(FPGA_IRQ_GPIO, &gpio_init);
	SYSCFG_EXTILineConfig(FPGA_IRQ_EXTI_PortSourceGPIO,
			      FPGA_IRQ_EXTI_PinSource);

	EXTI_InitTypeDef extiinit;
	EXTI_StructInit(&extiinit);
	extiinit.EXTI_Line = FPGA_IRQ_EXTI_Line;
	extiinit.EXTI_Mode = EXTI_Mode_Interrupt;
	extiinit.EXTI_Trigger = EXTI_Trigger_Rising;
	extiinit.EXTI_LineCmd = ENABLE;
	EXTI_Init(&extiinit);

	NVIC_InitTypeDef nvicinit;
	nvicinit.NVIC_IRQChannel = FPGA_IRQ_EXTI_IRQn;
	nvicinit.NVIC_IRQChannelPreemptionPriority = 2;
	nvicinit.NVIC_IRQChannelSubPriority = 3;
	nvicinit.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&nvicinit);

	//mostly to catch errors, transfer finish is by RX IRQ
	nvicinit.NVIC_IRQChannel = SPI_IRQn;
	nvicinit.NVIC_IRQChannelPreemptionPriority = 2;
	nvicinit.NVIC_IRQChannelSubPriority = 3;
	nvicinit.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&nvicinit);

	//TX_IRQ is not needed.
	nvicinit.NVIC_IRQChannel = SPI_RX_DMA_IRQn;
	nvicinit.NVIC_IRQChannelPreemptionPriority = 2;
	nvicinit.NVIC_IRQChannelSubPriority = 3;
	nvicinit.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&nvicinit);

	//could now just run the exti irq handler preemptively so it can check
	//for the source. the actual check would run in a defered work,
	//probably. and our spi code would have to learn to queue and handle
	//work packages.
}

static void setupDMA(void *read_data, void const *write_data, uint32_t length) {
	static uint32_t dummy_read;
	static uint32_t const dummy_write = 0xff;

	DMA_InitTypeDef dmainit;
	DMA_StructInit(&dmainit);

	dmainit.DMA_Channel = DMA_Channel_3;
	dmainit.DMA_PeripheralBaseAddr = (uint32_t)&(SPI_DEV->DR);
	if (read_data) {
		dmainit.DMA_Memory0BaseAddr = (uint32_t)read_data;
		dmainit.DMA_MemoryInc = DMA_MemoryInc_Enable;
	} else {
		dmainit.DMA_Memory0BaseAddr = (uint32_t)&dummy_read;
		dmainit.DMA_MemoryInc = DMA_MemoryInc_Disable;
	}
	dmainit.DMA_BufferSize = length;
	dmainit.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	dmainit.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	dmainit.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
	dmainit.DMA_Mode = DMA_Mode_Normal;
	dmainit.DMA_Priority = DMA_Priority_Low;
	dmainit.DMA_FIFOMode = DMA_FIFOMode_Disable;
	dmainit.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
	dmainit.DMA_MemoryBurst = DMA_MemoryBurst_Single;
	dmainit.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
	dmainit.DMA_DIR = DMA_DIR_PeripheralToMemory;
	DMA_Init(SPI_RX_DMA, &dmainit);

	dmainit.DMA_Channel = DMA_Channel_3;
	if (write_data) {
		dmainit.DMA_Memory0BaseAddr = (uint32_t)write_data;
		dmainit.DMA_MemoryInc = DMA_MemoryInc_Enable;
	} else {
		dmainit.DMA_Memory0BaseAddr = (uint32_t)&dummy_write;
		dmainit.DMA_MemoryInc = DMA_MemoryInc_Disable;
	}
	dmainit.DMA_DIR = DMA_DIR_MemoryToPeripheral;
	DMA_Init(SPI_TX_DMA, &dmainit);

	DMA_ClearFlag(SPI_RX_DMA, SPI_RX_DMA_CLEAR_FLAGS);
	DMA_ClearFlag(SPI_TX_DMA, SPI_TX_DMA_CLEAR_FLAGS);

	SPI_I2S_ITConfig(SPI_DEV, SPI_I2S_IT_ERR, ENABLE);
	DMA_ITConfig(SPI_RX_DMA, DMA_IT_TE | DMA_IT_TC, ENABLE);

	SPI_I2S_DMACmd(SPI_DEV, SPI_I2S_DMAReq_Rx, ENABLE);
	DMA_Cmd(SPI_RX_DMA, ENABLE);
	DMA_Cmd(SPI_TX_DMA, ENABLE);
	SPI_I2S_DMACmd(SPI_DEV, SPI_I2S_DMAReq_Tx, ENABLE);
}

static void issueCommand(FPGAComm_Command *command) {
	assert(!fpga_current_command);
	assert(isRPtr(command));
	fpga_current_command = command;

	//nss
	GPIO_ResetBits(SPI_NSS_GPIO, SPI_NSS_PIN);

	static uint32_t address;
	address = command->address;
	if (command->write_data)
		address |= 0x800000; // set WE bit
	else
		address &= ~0x800000; // strip WE bit

	command->state = 0;

	setupDMA(NULL, &address, 3);
}

void FPGAComm_ReadWriteCommand(FPGAComm_Command *command) {
	ISR_Guard g;
	assert(isRPtr(command));
	if (!fpga_current_command)
		issueCommand(command);
	else {
		for(auto &c : workqueue)
		  assert(isRPtr(c));
		workqueue.push_back(command);
		for(auto &c : workqueue)
		  assert(isRPtr(c));
	}
}

struct FPGAComm_FPGAComm_Command {
	uint32_t completed;
	FPGAComm_Command command;
};

static void FPGAComm_Completion(int result, FPGAComm_FPGAComm_Command *c) {
	if (result != 0) {
		FPGAComm_ReadWriteCommand(&c->command);
		return;
	}
	c->completed = 1;
}

void FPGAComm_CopyFromToFPGA(void *dest, uint32_t fpga, void const *src, size_t n) {
	assert(isRWPtr(dest) || dest == NULL);
	assert(isRPtr(src) || src == NULL);
	FPGAComm_FPGAComm_Command comm;
	comm.completed = 0;
	comm.command.address = fpga;
	comm.command.length = n;
	comm.command.read_data = dest;
	comm.command.write_data = src;
	comm.command.slot = sigc::bind(sigc::ptr_fun(&FPGAComm_Completion),&comm);

	FPGAComm_ReadWriteCommand(&comm.command);
	uint32_t volatile *t = (uint32_t volatile *)&comm.completed;
	while(!*t) {
		__WFI();
	}
}

void FPGAComm_CopyToFPGA(uint32_t dest, void const *src, size_t n) {
	FPGAComm_CopyFromToFPGA(NULL, dest, src, n);
}

void FPGAComm_CopyFromFPGA(void *dest, uint32_t src, size_t n) {
	FPGAComm_CopyFromToFPGA(dest, src, NULL, n);
}

static sigc::signal<void> FPGAComm_IRQHandlers[8];

sigc::signal<void> &FPGAComm_IRQHandler(unsigned int num) {
	return FPGAComm_IRQHandlers[num];
}

static uint8_t FPGAComm_IRQ_mask = 0;

void FPGAComm_EnableIRQs(unsigned int mask) {
	FPGAComm_IRQ_mask |= mask;
	FPGAComm_CopyToFPGA(FPGA_INT_IRQMSK, &FPGAComm_IRQ_mask, 1);
}

void FPGAComm_DisableIRQs(unsigned int mask) {
	FPGAComm_IRQ_mask &= ~mask;
	FPGAComm_CopyToFPGA(FPGA_INT_IRQMSK, &FPGAComm_IRQ_mask, 1);
}

void FPGAComm_EnableIRQs_nb(unsigned int mask, FPGAComm_Command *command) {
	FPGAComm_IRQ_mask |= mask;
	command->address = FPGA_INT_IRQMSK;
	command->length = 1;
	command->read_data = NULL;
	command->write_data = &FPGAComm_IRQ_mask;
	FPGAComm_ReadWriteCommand(command);
}

void FPGAComm_DisableIRQs_nb(unsigned int mask, FPGAComm_Command *command) {
	FPGAComm_IRQ_mask &= ~mask;
	command->address = FPGA_INT_IRQMSK;
	command->length = 1;
	command->read_data = NULL;
	command->write_data = &FPGAComm_IRQ_mask;
	FPGAComm_ReadWriteCommand(command);
}

static bool FPGAComm_IRQFetchInProgress = false;
static bool FPGAComm_IRQSeenAgain = false;
static uint8_t FPGAComm_IRQ_status;
static void FPGAComm_IRQFetch_Completion(int result);
static FPGAComm_Command FPGAComm_IRQFetch_Command = {
	.address = FPGA_INT_IRQSTS,
	.length = 1,
	.read_data = &FPGAComm_IRQ_status,
	.write_data = NULL,
	.slot = sigc::ptr_fun(&FPGAComm_IRQFetch_Completion),
	FPGAComm_Command_Private_Init,
};

static void FPGAComm_IRQFetch_Completion(int result) {
	if (result != 0) {
		FPGAComm_ReadWriteCommand(&FPGAComm_IRQFetch_Command);
		return;
	}
	uint8_t status;
	{
		ISR_Guard g;
		if (FPGAComm_IRQSeenAgain) {
			FPGAComm_ReadWriteCommand(&FPGAComm_IRQFetch_Command);
			return;
		} else {
			FPGAComm_IRQFetchInProgress = false;
			FPGAComm_IRQSeenAgain = false;
		}
		status = FPGAComm_IRQ_status;
	}
	for(unsigned i = 0; i < 7; i++) {
		if (status & (1<<i))
			FPGAComm_IRQHandlers[i]();
	}
}

void FPGA_IRQ_EXTI_IRQHandler() {
	if (EXTI_GetITStatus(FPGA_IRQ_EXTI_Line) == SET) {
		EXTI_ClearFlag(FPGA_IRQ_EXTI_Line);
		EXTI_ClearITPendingBit(FPGA_IRQ_EXTI_Line);

		ISR_Guard g;
		if (FPGAComm_IRQFetchInProgress) {
			FPGAComm_IRQSeenAgain = true;
		} else {
			FPGAComm_IRQFetchInProgress = true;

			FPGAComm_ReadWriteCommand(&FPGAComm_IRQFetch_Command);
		}
	}
}

void SPI_RX_DMA_IRQHandler() {
	if (DMA_GetITStatus(SPI_RX_DMA, SPI_RX_DMA_IT_TE)) {
		//nss
		GPIO_SetBits(SPI_NSS_GPIO, SPI_NSS_PIN);

		if (fpga_current_command->slot)
			fpga_current_command->slot(-1);

		SPI_I2S_ITConfig(SPI_DEV, SPI_I2S_IT_ERR, DISABLE);
		DMA_ITConfig(SPI_RX_DMA, DMA_IT_TE | DMA_IT_TC, DISABLE);
		DMA_Cmd(SPI_RX_DMA, DISABLE);
		DMA_Cmd(SPI_TX_DMA, DISABLE);
		SPI_I2S_DMACmd(SPI_DEV, SPI_I2S_DMAReq_Tx | SPI_I2S_DMAReq_Rx, DISABLE);
	} else {

		SPI_I2S_ITConfig(SPI_DEV, SPI_I2S_IT_ERR, DISABLE);
		DMA_ITConfig(SPI_RX_DMA, DMA_IT_TE | DMA_IT_TC, DISABLE);
		DMA_Cmd(SPI_RX_DMA, DISABLE);
		DMA_Cmd(SPI_TX_DMA, DISABLE);
		SPI_I2S_DMACmd(SPI_DEV, SPI_I2S_DMAReq_Tx | SPI_I2S_DMAReq_Rx, DISABLE);

		if(fpga_current_command->state == 0) {
			fpga_current_command->state = 1;

			setupDMA(fpga_current_command->read_data, fpga_current_command->write_data, fpga_current_command->length);
			return;
		} else {
			//nss
			GPIO_SetBits(SPI_NSS_GPIO, SPI_NSS_PIN);

			if (fpga_current_command->slot)
				fpga_current_command->slot(0);
		}
	}

	ISR_Guard g;
	fpga_current_command = NULL;
	if (!workqueue.empty()) {
		for(auto &c : workqueue)
		  assert(isRPtr(c));
		FPGAComm_Command *c = workqueue.front();
		assert(isRPtr(c));
		workqueue.pop_front();
		for(auto &c : workqueue)
		  assert(isRPtr(c));
		issueCommand(c);
		for(auto &c : workqueue)
		  assert(isRPtr(c));
	}
}

void SPI_IRQHandler() {
	//we only ever get here on error.
	SPI_I2S_ITConfig(SPI_DEV, SPI_I2S_IT_ERR, DISABLE);
	DMA_ITConfig(SPI_RX_DMA, DMA_IT_TE | DMA_IT_TC, DISABLE);
	DMA_Cmd(SPI_RX_DMA, DISABLE);
	DMA_Cmd(SPI_TX_DMA, DISABLE);
	SPI_I2S_DMACmd(SPI_DEV, SPI_I2S_DMAReq_Tx | SPI_I2S_DMAReq_Rx, DISABLE);

	//nss
	GPIO_SetBits(SPI_NSS_GPIO, SPI_NSS_PIN);

	if (fpga_current_command->slot)
		fpga_current_command->slot(-1);

	ISR_Guard g;
	fpga_current_command = NULL;
	if (!workqueue.empty()) {
		for(auto &c : workqueue)
		  assert(isRPtr(c));
		FPGAComm_Command *c = workqueue.front();
		assert(isRPtr(c));
		workqueue.pop_front();
		for(auto &c : workqueue)
		  assert(isRPtr(c));
		issueCommand(c);
		for(auto &c : workqueue)
		  assert(isRPtr(c));
	}
}

