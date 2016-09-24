#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_tim.h"
#include "stm32f4xx_spi.h"

static void delay(int count) {
  volatile int c = count*1000;
  while(c > 0) c--;
}

static void GPIO_Setup() {
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA |
			       RCC_AHB1Periph_GPIOB |
			       RCC_AHB1Periph_GPIOC |
			       RCC_AHB1Periph_GPIOD |
			       RCC_AHB1Periph_GPIOH, ENABLE);
	GPIO_InitTypeDef gpio_init;
	GPIO_StructInit(&gpio_init);
	gpio_init.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3;
	gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_Init(GPIOA, &gpio_init);
	GPIO_SetBits(GPIOA, GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3);
}

static int check_pin(GPIO_TypeDef *gpio, uint16_t pin) {
	GPIO_InitTypeDef gpio_init;
	GPIO_StructInit(&gpio_init);
	gpio_init.GPIO_Pin = pin;
	gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_IN;
	gpio_init.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(gpio, &gpio_init);
	delay(1);
	int r = GPIO_ReadInputDataBit(gpio, pin)?0:1;
	gpio_init.GPIO_PuPd = GPIO_PuPd_DOWN;
	GPIO_Init(gpio, &gpio_init);
	delay(1);
	r |= !GPIO_ReadInputDataBit(gpio, pin)?0:2;
	gpio_init.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(gpio, &gpio_init);
	return r;
}

static int check_pin_pair(GPIO_TypeDef *gpio1, uint16_t pin1,
			  GPIO_TypeDef *gpio2, uint16_t pin2) {
	GPIO_InitTypeDef gpio_init;
	GPIO_StructInit(&gpio_init);
	gpio_init.GPIO_Pin = pin1;
	gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_IN;
	gpio_init.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(gpio1, &gpio_init);
	gpio_init.GPIO_Pin = pin2;
	gpio_init.GPIO_PuPd = GPIO_PuPd_DOWN;
	GPIO_Init(gpio2, &gpio_init);
	delay(1);
	int r = GPIO_ReadInputDataBit(gpio1, pin1)?0:1;
	r |= !GPIO_ReadInputDataBit(gpio2, pin2)?0:2;

	gpio_init.GPIO_Pin = pin1;
	gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_IN;
	gpio_init.GPIO_PuPd = GPIO_PuPd_DOWN;
	GPIO_Init(gpio1, &gpio_init);
	gpio_init.GPIO_Pin = pin2;
	gpio_init.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(gpio2, &gpio_init);
	delay(1);
	r |= !GPIO_ReadInputDataBit(gpio1, pin1)?0:4;
	r |= GPIO_ReadInputDataBit(gpio2, pin2)?0:8;

	gpio_init.GPIO_Pin = pin1;
	gpio_init.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(gpio1, &gpio_init);
	gpio_init.GPIO_Pin = pin2;
	gpio_init.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(gpio2, &gpio_init);
	return r;
}

void SPI_RW(uint16_t addr, void *rd, void *wr, size_t count) {
	GPIO_ResetBits(GPIOA, GPIO_Pin_4);
	delay(1);
	if (wr)
		addr |= 0x8000; // set WE bit
	else
		addr &= ~0x8000; // strip WE bit
	//send first address byte
	SPI_I2S_SendData(SPI1, ((uint8_t*)&addr)[0]);
	//receive dummy result from address transfer
	while (SPI_I2S_GetFlagStatus(SPI1, SPI_FLAG_RXNE) == RESET)
	{}
	SPI_I2S_ReceiveData(SPI1);

	//send second address byte
	while (SPI_I2S_GetFlagStatus(SPI1, SPI_FLAG_TXE) == RESET)
	{}
	SPI_I2S_SendData(SPI1, ((uint8_t*)&addr)[1]);
	//receive dummy result from address transfer
	while (SPI_I2S_GetFlagStatus(SPI1, SPI_FLAG_RXNE) == RESET)
	{}
	SPI_I2S_ReceiveData(SPI1);

	uint8_t *pw = (uint8_t*)wr;
	uint8_t *pr = (uint8_t*)rd;
	while (count > 0) {
		//send data byte
		while (SPI_I2S_GetFlagStatus(SPI1, SPI_FLAG_TXE) == RESET)
		{}
		if (pw)
			SPI_I2S_SendData(SPI1, *pw++);
		else
			SPI_I2S_SendData(SPI1, 0);
		//receive result from "memory"
		while (SPI_I2S_GetFlagStatus(SPI1, SPI_FLAG_RXNE) == RESET)
		{}
		if (pr)
			*pr++ = SPI_I2S_ReceiveData(SPI1);
		else
			SPI_I2S_ReceiveData(SPI1);
		count--;
	}
	GPIO_SetBits(GPIOA, GPIO_Pin_4);
	delay(1);
}

void SPI_Read(uint16_t addr, void *rd, size_t count) {
	SPI_RW(addr, rd, NULL, count);
}

void SPI_Write(uint16_t addr, void *wr, size_t count) {
	SPI_RW(addr, NULL, wr, count);
}

int main()
{
	GPIO_Setup();

	volatile uint32_t r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0;
	r1 |= check_pin(GPIOH, GPIO_Pin_0) << 0;
	r1 |= check_pin(GPIOA, GPIO_Pin_0) << 2;
	r1 |= check_pin(GPIOA, GPIO_Pin_4) << 4;
	r1 |= check_pin(GPIOA, GPIO_Pin_5) << 6;
	// driven by fpga when NSS happens to be low. must allow either error.
	r1 |= check_pin(GPIOA, GPIO_Pin_6) << 8;
	r1 |= check_pin(GPIOA, GPIO_Pin_7) << 10;
	// PA8 has pull up, expecting 0x2
	r1 |= check_pin(GPIOA, GPIO_Pin_8) << 12;
	r1 |= check_pin(GPIOA, GPIO_Pin_11) << 14;
	r1 |= check_pin(GPIOA, GPIO_Pin_12) << 16;
	r1 |= check_pin(GPIOB, GPIO_Pin_5) << 18;
	r1 |= check_pin(GPIOB, GPIO_Pin_7) << 20;
	r1 |= check_pin(GPIOB, GPIO_Pin_8) << 22;
	r1 |= check_pin(GPIOB, GPIO_Pin_9) << 24;
	r1 |= check_pin(GPIOB, GPIO_Pin_10) << 26;
	r1 |= check_pin(GPIOB, GPIO_Pin_11) << 28;
	//PB12 is connected to fpga DOUT, which is used during configuration.
	//good to know. expecting 2
	r1 |= check_pin(GPIOB, GPIO_Pin_12) << 30;
	r2 |= check_pin(GPIOB, GPIO_Pin_13) << 0;
	r2 |= check_pin(GPIOB, GPIO_Pin_14) << 2;
	r2 |= check_pin(GPIOB, GPIO_Pin_15) << 4;
	r2 |= check_pin(GPIOC, GPIO_Pin_4) << 6;
	r2 |= check_pin(GPIOC, GPIO_Pin_8) << 8;
	r2 |= check_pin(GPIOC, GPIO_Pin_9) << 10;
	r2 |= check_pin(GPIOC, GPIO_Pin_10) << 12;
	r2 |= check_pin(GPIOC, GPIO_Pin_11) << 14;
	r2 |= check_pin(GPIOC, GPIO_Pin_12) << 16;
	r2 |= check_pin(GPIOD, GPIO_Pin_2) << 18;

	r3 |= check_pin_pair(GPIOA, GPIO_Pin_4, GPIOA, GPIO_Pin_5) << 0;
	//PA6 driven by fpga when NSS happens to be low. must allow either error.
	// expecting either 0x8 or 0x2
	r3 |= check_pin_pair(GPIOA, GPIO_Pin_5, GPIOA, GPIO_Pin_6) << 4;
	//PA6 driven by fpga when NSS happens to be low. must allow either error.
	// expecting either 0x4 or 0x1
	r3 |= check_pin_pair(GPIOA, GPIO_Pin_6, GPIOA, GPIO_Pin_7) << 8;
	r3 |= check_pin_pair(GPIOA, GPIO_Pin_7, GPIOC, GPIO_Pin_4) << 12;
	r3 |= check_pin_pair(GPIOB, GPIO_Pin_10, GPIOB, GPIO_Pin_11) << 16;
	//PB12 is fpga DOUT, expecting 0x4
	r3 |= check_pin_pair(GPIOB, GPIO_Pin_12, GPIOB, GPIO_Pin_13) << 20;
	r3 |= check_pin_pair(GPIOB, GPIO_Pin_13, GPIOB, GPIO_Pin_14) << 24;
	r3 |= check_pin_pair(GPIOB, GPIO_Pin_14, GPIOB, GPIO_Pin_15) << 28;
	r4 |= check_pin_pair(GPIOC, GPIO_Pin_8, GPIOC, GPIO_Pin_9) << 0;
	//pull up above on PA8, expecting 0x2
	r4 |= check_pin_pair(GPIOC, GPIO_Pin_9, GPIOA, GPIO_Pin_8) << 4;
	r4 |= check_pin_pair(GPIOA, GPIO_Pin_11, GPIOA, GPIO_Pin_12) << 8;
	r4 |= check_pin_pair(GPIOC, GPIO_Pin_10, GPIOC, GPIO_Pin_11) << 12;
	r4 |= check_pin_pair(GPIOC, GPIO_Pin_11, GPIOC, GPIO_Pin_12) << 16;
	r4 |= check_pin_pair(GPIOC, GPIO_Pin_12, GPIOD, GPIO_Pin_2) << 20;
	r4 |= check_pin_pair(GPIOB, GPIO_Pin_8, GPIOB, GPIO_Pin_9) << 24;

	r5 |= check_pin_pair(GPIOD, GPIO_Pin_2, GPIOC, GPIO_Pin_11) << 0;
	r5 |= check_pin_pair(GPIOB, GPIO_Pin_11, GPIOB, GPIO_Pin_14) << 4;
	//PB12 is fpga DOUT, expecting 0x4
	r5 |= check_pin_pair(GPIOB, GPIO_Pin_12, GPIOB, GPIO_Pin_15) << 8;
	r5 |= check_pin_pair(GPIOB, GPIO_Pin_15, GPIOB, GPIO_Pin_13) << 12;
	r5 |= check_pin_pair(GPIOB, GPIO_Pin_9, GPIOB, GPIO_Pin_7) << 16;
	r5 |= check_pin_pair(GPIOB, GPIO_Pin_7, GPIOB, GPIO_Pin_8) << 20;
	r5 |= check_pin_pair(GPIOB, GPIO_Pin_8, GPIOC, GPIO_Pin_4) << 24;
	r5 |= check_pin_pair(GPIOH, GPIO_Pin_0, GPIOA, GPIO_Pin_4) << 28;

	enum { TestPullNo, TestPullDown, TestPullUp, TestPullMixed } testmode;

	GPIO_ResetBits(GPIOA, GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3);
	// case 1: unprogrammed fpga
	if(r1 == 0x80002000 && r2 == 0x00000000 &&
	   r3 == 0x00400000 && r4 == 0x00000020 &&
	   r5 == 0x00000400) {
		GPIO_SetBits(GPIOA, GPIO_Pin_2);
		GPIO_SetBits(GPIOA, GPIO_Pin_1);
		while(1) {} //__WFI();
	}
	// case 2: fpga programmed with test program, no pull mode.
	else if ((r1 == 0x00002000 || r1 == 0x00002100 || r1 == 0x00002200) &&
		 r2 == 0x00000000 &&
		 (r3 == 0x00000000 || r3 == 0x00000180 || r3 == 0x00000420) &&
		 r4 == 0x00000020 &&
		 r5 == 0x00000000) {
		GPIO_SetBits(GPIOA, GPIO_Pin_1);
		testmode = TestPullNo;
	}
	// case 3: fpga programmed with test program, pull down mode.
	else if ((r1 == 0x55402005 || r1 == 0x55402105 || r1 == 0x55402205) &&
		 r2 == 0x00000055 &&
		 (r3 == 0x99998000 || r3 == 0x99998180 || r3 == 0x99998420) &&
		 r4 == 0x09000020 &&
		 r5 == 0x19819990) {
		GPIO_SetBits(GPIOA, GPIO_Pin_1);
		testmode = TestPullDown;
	}
	// case 4: fpga programmed with test program, pull up mode.
	// pull ups seem to be somewhat weaker than the pull downs of the stm32.
	else if ((r1 == 0xaa80200a || r1 == 0xaa80210a || r1 == 0xaa80220a ||
		  r1 == 0x00002000 || r1 == 0x00002100 || r1 == 0x00002200) &&
		 r2 == 0x000000aa &&
		 (r3 == 0x66662000 || r3 == 0x66662180 || r3 == 0x66662420 ||
		  r3 == 0x00000000 || r3 == 0x00000180 || r3 == 0x00000420) &&
		 r4 == 0x06000020 &&
		 r5 == 0x46246660) {
		GPIO_SetBits(GPIOA, GPIO_Pin_1);
		testmode = TestPullUp;
	}
	// case 5: fpga programmed with test program, mixed pull mode.
	else if ((r1 == 0x9a802006 || r1 == 0x9a802106 || r1 == 0x9a802206 ||
		  r1 == 0x10002004 || r1 == 0x10002104 || r1 == 0x10002204) &&
		 r2 == 0x0000005a &&
		 (r3 == 0xc66c8000 || r3 == 0xc66c8180 || r3 == 0xc66c8420 ||
		  r3 == 0xc00c8000 || r3 == 0xc00c8180 || r3 == 0xc00c8420) &&
		 r4 == 0x06000020 &&
		 r5 == 0x4c243c30) {
		GPIO_SetBits(GPIOA, GPIO_Pin_1);
		testmode = TestPullMixed;
	}
	else {
		GPIO_SetBits(GPIOA, GPIO_Pin_3);
		while(1) {} //__WFI();
	}

	//drive XS_NSS(PA4) high and check XS_MOSI(PA6) again.
	GPIO_InitTypeDef gpio_init;
	GPIO_StructInit(&gpio_init);
	gpio_init.GPIO_Pin = GPIO_Pin_4;
	gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_Init(GPIOA, &gpio_init);
	GPIO_SetBits(GPIOA, GPIO_Pin_4);

	r1 = 0x00002000; r3 = 0x00000000;
	r1 |= check_pin(GPIOA, GPIO_Pin_6) << 8;
	r3 |= check_pin_pair(GPIOA, GPIO_Pin_5, GPIOA, GPIO_Pin_6) << 4;
	r3 |= check_pin_pair(GPIOA, GPIO_Pin_6, GPIOA, GPIO_Pin_7) << 8;
	if (r1 == 0x00002000 && r3 == 0x00000000)
		GPIO_SetBits(GPIOA, GPIO_Pin_1);
	else {
		GPIO_SetBits(GPIOA, GPIO_Pin_3);
		while(1) {} //__WFI();
	}

	// okay, need to setup TIM4_CH2(PB7) and try some SPI commands.
	gpio_init.GPIO_Pin = GPIO_Pin_7;
	gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_AF;
	GPIO_Init(GPIOB, &gpio_init);
	GPIO_PinAFConfig(GPIOB, GPIO_PinSource7, GPIO_AF_TIM4);

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);
	TIM_TimeBaseInitTypeDef timebaseinit;
	TIM_OCInitTypeDef ocinit;
	TIM_TimeBaseStructInit(&timebaseinit);
	TIM_OCStructInit(&ocinit);

	timebaseinit.TIM_Prescaler = 0;
	timebaseinit.TIM_CounterMode = TIM_CounterMode_Up;
	timebaseinit.TIM_Period = 4-1;
	timebaseinit.TIM_ClockDivision = TIM_CKD_DIV1;
	timebaseinit.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(TIM4, &timebaseinit);
	ocinit.TIM_OCMode = TIM_OCMode_PWM1;
	ocinit.TIM_OutputState = TIM_OutputState_Enable;
	ocinit.TIM_Pulse = 2;
	ocinit.TIM_OCPolarity = TIM_OCPolarity_High;
	TIM_OC2Init(TIM4, &ocinit);
	TIM_Cmd(TIM4, ENABLE);

	//setup SPI
	gpio_init.GPIO_Pin = GPIO_Pin_4; //NSS
	gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_Init(GPIOA, &gpio_init);
	GPIO_SetBits(GPIOA, GPIO_Pin_4);

	gpio_init.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
	gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_AF;
	GPIO_Init(GPIOA, &gpio_init);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource5, GPIO_AF_SPI1);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource6, GPIO_AF_SPI1);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource7, GPIO_AF_SPI1);

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);

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
	spi_init.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_64;
	spi_init.SPI_FirstBit = SPI_FirstBit_LSB;
	SPI_Init(SPI1, &spi_init);
	SPI_Cmd(SPI1, ENABLE);

	volatile uint32_t v1 = 0;
	volatile uint32_t v2 = 0xdeadbeef;
	volatile uint32_t v3 = 0;
	SPI_Read(0, (void*)&v1, 4);
	SPI_Write(0, (void*)&v2, 4);
	SPI_Read(0, (void*)&v3, 4);

	if (v2 == v3)
		GPIO_SetBits(GPIOA, GPIO_Pin_1);
	else {
		GPIO_ResetBits(GPIOA, GPIO_Pin_1 | GPIO_Pin_2);
		GPIO_SetBits(GPIOA, GPIO_Pin_3);
		while(1) {} //__WFI();
	}

	SPI_Read(0x7ffd, NULL, 0);

	//communication established!. now check what the pins say and if it
	//matches the current test mode.
	char output_pins[9];
	SPI_Read(28, (void*)output_pins, 9);

	if (testmode == TestPullNo) {
		//nothing to test about this.
	} else if (testmode == TestPullDown) {
		if (output_pins[0] == 0x00 && output_pins[1] == 0x00 &&
		    output_pins[2] == 0x00 && output_pins[3] == 0x00 &&
		    output_pins[4] == 0x00 && output_pins[5] == 0x00 &&
		    output_pins[6] == 0x00 && output_pins[7] == 0x00 &&
		    output_pins[8] == 0x00)
			GPIO_SetBits(GPIOA, GPIO_Pin_1);
		else {
			GPIO_ResetBits(GPIOA, GPIO_Pin_1 | GPIO_Pin_2);
			GPIO_SetBits(GPIOA, GPIO_Pin_3);
			while(1) {} //__WFI();
		}
	} else if (testmode == TestPullUp) {
		if (output_pins[0] == 0xff && output_pins[1] == 0xff &&
		    output_pins[2] == 0xff && output_pins[3] == 0xff &&
		    output_pins[4] == 0xff && output_pins[5] == 0xff &&
		    output_pins[6] == 0xff && output_pins[7] == 0xff &&
		    output_pins[8] == 0x07)
			GPIO_SetBits(GPIOA, GPIO_Pin_1);
		else {
			GPIO_ResetBits(GPIOA, GPIO_Pin_1 | GPIO_Pin_2);
			GPIO_SetBits(GPIOA, GPIO_Pin_3);
			while(1) {} //__WFI();
		}
	} else if (testmode == TestPullMixed) {

		//errors:
		// output_pins[1] == 0xcc => FA(4) pulled high -- this may actually be a problem
		// output_pins[4] == 0x00 => FD(3), FD(4) pulled low -- this probably not a problem, the level shifter probably drives.
		// output_pins[5] == 0xff => BLO_OUT,GLO_OUT,RHI_OUT pulled high
		// the RGB outputs interact, so no wonder.
		// BHI => h, BLO => l, GHI => h, GLO => l, RHI => l, RLO => h
		if (output_pins[0] == 0xee && output_pins[1] == 0x4c &&
		    output_pins[2] == 0x7d &&
		    (output_pins[3] & 0x7f) == 0x31 && // FD pins
		    (output_pins[4] & 0x80) == 0x00 &&
		    (output_pins[5] & 0x81) == 0x81 && // RGB pins
		    output_pins[6] == 0x91 && output_pins[7] == 0xd9 &&
		    output_pins[8] == 0x00)
			GPIO_SetBits(GPIOA, GPIO_Pin_1);
		else {
			GPIO_ResetBits(GPIOA, GPIO_Pin_1 | GPIO_Pin_2);
			GPIO_SetBits(GPIOA, GPIO_Pin_3);
			while(1) {} //__WFI();
		}
		// now force ADIR/ANOE/DDIR/DNOE high and check again
		uint8_t b = 0x07;
		SPI_Write(12, (void*)&b, 1); // _I = 1
		b = 0xf8;
		SPI_Write(24, (void*)&b, 1); // _T = 0 (output)
		b = 0x80;
		SPI_Write(11, (void*)&b, 1);
		b = 0x7f;
		SPI_Write(23, (void*)&b, 1);

		SPI_Read(28, (void*)output_pins, 9);
		if (output_pins[3] == 0x31 && // FD pins
		    output_pins[4] == 0x0c)
			GPIO_SetBits(GPIOA, GPIO_Pin_1);
		else {
			GPIO_ResetBits(GPIOA, GPIO_Pin_1 | GPIO_Pin_2);
			GPIO_SetBits(GPIOA, GPIO_Pin_3);
			while(1) {} //__WFI();
		}
	}

	// now, write some real patterns and observe?

	GPIO_ResetBits(GPIOA, GPIO_Pin_1);
	GPIO_SetBits(GPIOA, GPIO_Pin_2);
	while(1) {} //__WFI();
	return 0;
}

