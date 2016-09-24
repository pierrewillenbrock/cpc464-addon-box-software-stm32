
#include "fpga_comm.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_spi.h"
#include "stm32f4xx_rcc.h"

#define SPI_NSS_PIN GPIO_Pin_4
#define SPI_NSS_GPIO GPIOA
#define SPI_GPIO GPIOA
#define SPI_PINS (GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7)
#define SPI_GPIO_RCC RCC_AHB1Periph_GPIOA
#define SPI_DEV SPI1
#define SPI_AF GPIO_AF_SPI1
#define SPI_RCC_FUNC RCC_APB2PeriphClockCmd
#define SPI_RCC RCC_APB2Periph_SPI1

static void delay(int count) {
  volatile int c = count*1000;
  while(c > 0) c--;
}

void FPGAComm_Setup() {
	RCC_AHB1PeriphClockCmd(SPI_GPIO_RCC, ENABLE);

	GPIO_InitTypeDef gpio_init;
	GPIO_SetBits(SPI_NSS_GPIO, SPI_NSS_PIN);
	GPIO_StructInit(&gpio_init);
	gpio_init.GPIO_Pin = SPI_NSS_PIN;
	gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_Init(SPI_NSS_GPIO, &gpio_init);

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
	spi_init.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_32;
	spi_init.SPI_FirstBit = SPI_FirstBit_LSB;
	SPI_Init(SPI_DEV, &spi_init);
	SPI_Cmd(SPI_DEV, ENABLE);
}

void FPGAComm_CopyFromToFPGA(void *dest, uint16_t fpga, void const *src, size_t n) {
	GPIO_ResetBits(GPIOA, GPIO_Pin_4);
	delay(1);
	if (src)
		fpga |= 0x8000; // set WE bit
	else
		fpga &= ~0x8000; // strip WE bit
	//send first fpgaess byte
	SPI_I2S_SendData(SPI_DEV, ((uint8_t*)&fpga)[0]);
	//receive dummy result from fpgaess transfer
	while (SPI_I2S_GetFlagStatus(SPI_DEV, SPI_FLAG_RXNE) == RESET)
	{}
	SPI_I2S_ReceiveData(SPI_DEV);

	//send second fpgaess byte
	while (SPI_I2S_GetFlagStatus(SPI_DEV, SPI_FLAG_TXE) == RESET)
	{}
	SPI_I2S_SendData(SPI_DEV, ((uint8_t*)&fpga)[1]);
	//receive dummy result from fpgaess transfer
	while (SPI_I2S_GetFlagStatus(SPI_DEV, SPI_FLAG_RXNE) == RESET)
	{}
	SPI_I2S_ReceiveData(SPI_DEV);

	uint8_t *ps = (uint8_t*)src;
	uint8_t *pd = (uint8_t*)dest;
	while (n > 0) {
		//send data byte
		while (SPI_I2S_GetFlagStatus(SPI_DEV, SPI_FLAG_TXE) == RESET)
		{}
		if (ps)
			SPI_I2S_SendData(SPI_DEV, *ps++);
		else
			SPI_I2S_SendData(SPI_DEV, 0);
		//receive result from "memory"
		while (SPI_I2S_GetFlagStatus(SPI_DEV, SPI_FLAG_RXNE) == RESET)
		{}
		if (pd)
			*pd++ = SPI_I2S_ReceiveData(SPI_DEV);
		else
			SPI_I2S_ReceiveData(SPI_DEV);
		n--;
	}
	GPIO_SetBits(GPIOA, GPIO_Pin_4);
	delay(1);
}

void FPGAComm_CopyToFPGA(uint16_t dest, void const *src, size_t n) {
	FPGAComm_CopyFromToFPGA(NULL, dest, src, n);
}

void FPGAComm_CopyFromFPGA(void *dest, uint16_t src, size_t n) {
	FPGAComm_CopyFromToFPGA(dest, src, NULL, n);
}

