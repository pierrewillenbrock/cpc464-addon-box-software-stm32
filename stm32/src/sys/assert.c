
#include <stdint.h>
#include <bsp/misc.h>
#include <bsp/stm32f4xx_conf.h>
#include <bsp/stm32f4xx_gpio.h>
#include <assert.h>
#include <bits.h>
#include <hw/led.h>

volatile const char *assert_error_file;
volatile unsigned int assert_error_line;
volatile const char *assert_error_func;
volatile const char *assert_error_expr;

/*******************************************************************************
* Function Name  : assert_failed
* Description    : Reports the name of the source file and the source line number
*                  where the assert_param error has occurred.
* Input          : - file: pointer to the source file name
*                  - line: assert_param error line source number
* Output         : None
* Return         : None
*******************************************************************************/
void assert_failed(uint8_t* file, uint32_t line)
{
  __assert_func((const char*)file, (int)line, "", "");
}

void __assert_func(const char *file, int line, const char *func, const char * expr) {
	__disable_irq();
	assert_error_file = file;
	assert_error_line = line;
	assert_error_func = func;
	assert_error_expr = expr;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
	GPIO_InitTypeDef gpio_init;
	GPIO_StructInit(&gpio_init);
	gpio_init.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3;
	gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_Init(GPIOA, &gpio_init);
	volatile int delay;
	int waittime = 500000;

	GPIO_ResetBits(LED_GPIO, LEDG_PIN | LEDB_PIN);

	while(1)
	{
		GPIO_SetBits(LED_GPIO, LEDR_PIN);
		delay = waittime;
		while(delay) {
			delay--;
		}

		GPIO_ResetBits(LED_GPIO, LEDR_PIN);
		delay = waittime;
		while(delay) {
			delay--;
		}
	}
}

void NMI_Handler(void)
{
	assert_failed((uint8_t *)__FILE__, __LINE__);
}

/* need to have that globally, gcc checks only at the end when the diagnostic
 * scope is left already */
#pragma GCC diagnostic ignored "-Wunused-variable"
static SCB_Type * scb = SCB;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static void FatalExceptionHandler(struct ARMv7M_FPUExceptionFrame * fp) {
	assert_failed((uint8_t *)__FILE__, __LINE__);
}
#pragma GCC diagnostic pop

void HardFault_Handler(void) __attribute__((naked));
void HardFault_Handler(void)
{
	__ASM volatile ("mov r0, sp"
		"\n\tbl FatalExceptionHandler"
		: : "X"(FatalExceptionHandler) : "r0"
		);
}

void BusFault_Handler(void) __attribute__((naked));
void BusFault_Handler(void)
{
	__ASM volatile ("mov r0, sp"
		"\n\tbl FatalExceptionHandler"
		: : "X"(FatalExceptionHandler) : "r0"
		);
}

void UsageFault_Handler(void) __attribute__((naked));
void UsageFault_Handler(void)
{
	__ASM volatile ("mov r0, sp"
		"\n\tbl FatalExceptionHandler"
		: : "X"(FatalExceptionHandler) : "r0"
		);
}

