

MEMORY
{
	FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
	STACK  : ORIGIN = 0x2001e000, LENGTH = 8K
	RAM   (!rwx) : ORIGIN = 0x20000000, LENGTH = 120K
}

/* inital stack pointer at end of memory */
PROVIDE (_estack = 0x20020000);/* gets hard coded into vector table */
PROVIDE (_bstack = 0x2001e000);
PROVIDE (_eheap = 0x2001e000);
SECTIONS
{
	.  = 0x08000000;          /* From 0x00000000 */
	. = ALIGN(4);
	.isr_vector : {
		KEEP(*(.isr_vector))            /* Startup code */
	} >FLASH AT > FLASH
	/*  */
	. = ALIGN(4);
	/* the init sections are never written since we are not relocatable */
	.init : {
		KEEP(*(.init))             /* Startup code from .init-section */
	} >FLASH AT > FLASH
	. = ALIGN(4);
	.preinit_array     :
	{
		PROVIDE (__preinit_array_start = .);
		KEEP (*(.preinit_array*))
		PROVIDE (__preinit_array_end = .);
	} >FLASH AT > FLASH
	.init_array : {
		PROVIDE (__init_array_start = .) ;
		KEEP (*(SORT(.init_array.*)))
		KEEP (*(.init_array))
		PROVIDE (__init_array_end = .) ;
	} >FLASH AT > FLASH
	. = ALIGN(4);
	.text : {
		*(.text)        /* Program code */
		*(.text*)       /* remaining code */
	} >FLASH AT > FLASH
	.rodata : {
		*(.rodata)      /* Read only data */
		*(.rodata*)
	} >FLASH AT > FLASH
	.ARM.extab : {
		*(.ARM.extab* .ARM.extab.gnu.linkonce.armextab.*)
	} > FLASH AT > FLASH

	. = .;
	__exidx_start = .;
	.ARM.exidx : {
		*(.ARM.exidx* .ARM.exidx.gnu.linkonce.armexidx.*)
	} > FLASH AT > FLASH
	__exidx_end = .;

	/* the got and .data.rel.ro* sections are never written since we are
         * not relocatable.
	 * Background:
	 * The .got and .data.rel.ro* sections contain data that would need
	 * to be relocated by the dynamic linker. afterwards, these sections
	 * should be considered read only. We do a static link into a fixed
	 * memory, so we can make them read only from the beginning.
	 */
	.got : {
		*(.got)
		*(.got*)
	} >FLASH AT > FLASH
	.data.rel.ro : {
		*(.data.rel.ro*)
	} >FLASH AT > FLASH

	. = ALIGN(4); /* automatically placed sections ld wants to place here */
	_etext = .;
	_sidata = _etext;

	.data : {
		/* This is used by the startup in order to initialize the
		 * .data secion. it must appear inside this section so the
		 * linker knows the right vma(start of RAM). */
		PROVIDE (_sdata = .) ;

		*(.data)
		*(.data*)
	} >RAM AT > FLASH

	. = ALIGN(4);

	.memfunc : {
		*(.memfunc)
		*(.memfunc*)
	} >RAM AT > FLASH

	. = ALIGN(4);

	. = ALIGN(4); /* automatically placed sections ld wants to place here */

	/* This is used by the startup in order to initialize the .data secion */
	PROVIDE (_edata = .);
	/* This is used by the startup in order to initialize the .bss secion */
	PROVIDE (_sbss = .);

	/* This is the uninitialized data section */
	.bss :
	{
		*(.bss)
		*(.bss*)
		*(COMMON)
	} >RAM AT > RAM

	. = .;

	/* This is used by the startup in order to initialize the .bss secion */
	PROVIDE (_ebss = .);
	/* bheap < 0x20010000:
	 align(128): uses up to 3 MPU slots
	 align(1024): uses up to 2 MPU slots
	 bheap < 0x20020000:
	 align(256): uses up to 3 MPU slots
	 align(2048): uses up to 2 MPU slots
	 */
	. = ALIGN(128);
	PROVIDE (_bheap = .);
}

/*  Libraries to link against. */

/*INPUT( -lstm32fw )*/

/*  Provide a default vector for any unhandled interrupts. */

PROVIDE(NMI_Handler = _default_handler);
PROVIDE(HardFault_Handler = _default_handler);
PROVIDE(MemManage_Handler = _default_handler);
PROVIDE(BusFault_Handler = _default_handler);
PROVIDE(UsageFault_Handler = _default_handler);
PROVIDE(DebugMon_Handler = _default_handler);
PROVIDE(SVC_Handler = _default_handler);
PROVIDE(PendSV_Handler = _default_handler);
PROVIDE(SysTick_Handler = _default_handler);
PROVIDE(WWDG_IRQHandler = _default_handler);
PROVIDE(PVD_IRQHandler = _default_handler);
PROVIDE(TAMPER_IRQHandler = _default_handler);
PROVIDE(RTC_IRQHandler = _default_handler);
PROVIDE(FLASH_IRQHandler = _default_handler);
PROVIDE(RCC_IRQHandler = _default_handler);
PROVIDE(EXTI0_IRQHandler = _default_handler);
PROVIDE(EXTI1_IRQHandler = _default_handler);
PROVIDE(EXTI2_IRQHandler = _default_handler);
PROVIDE(EXTI3_IRQHandler = _default_handler);
PROVIDE(EXTI4_IRQHandler = _default_handler);
PROVIDE(DMA1_Channel1_IRQHandler = _default_handler);
PROVIDE(DMA1_Channel2_IRQHandler = _default_handler);
PROVIDE(DMA1_Channel3_IRQHandler = _default_handler);
PROVIDE(DMA1_Channel4_IRQHandler = _default_handler);
PROVIDE(DMA1_Channel5_IRQHandler = _default_handler);
PROVIDE(DMA1_Channel6_IRQHandler = _default_handler);
PROVIDE(DMA1_Channel7_IRQHandler = _default_handler);
PROVIDE(ADC1_2_IRQHandler = _default_handler);
PROVIDE(USB_HP_CAN_TX_IRQHandler = _default_handler);
PROVIDE(USB_LP_CAN_RX0_IRQHandler = _default_handler);
PROVIDE(CAN_RX1_IRQHandler = _default_handler);
PROVIDE(CAN_SCE_IRQHandler = _default_handler);
PROVIDE(EXTI9_5_IRQHandler = _default_handler);
PROVIDE(TIM1_BRK_IRQHandler = _default_handler);
PROVIDE(TIM1_UP_IRQHandler = _default_handler);
PROVIDE(TIM1_TRG_COM_IRQHandler = _default_handler);
PROVIDE(TIM1_CC_IRQHandler = _default_handler);
PROVIDE(TIM2_IRQHandler = _default_handler);
PROVIDE(TIM3_IRQHandler = _default_handler);
PROVIDE(TIM4_IRQHandler = _default_handler);
PROVIDE(I2C1_EV_IRQHandler = _default_handler);
PROVIDE(I2C1_ER_IRQHandler = _default_handler);
PROVIDE(I2C2_EV_IRQHandler = _default_handler);
PROVIDE(I2C2_ER_IRQHandler = _default_handler);
PROVIDE(SPI1_IRQHandler = _default_handler);
PROVIDE(SPI2_IRQHandler = _default_handler);
PROVIDE(USART1_IRQHandler = _default_handler);
PROVIDE(USART2_IRQHandler = _default_handler);
PROVIDE(USART3_IRQHandler = _default_handler);
PROVIDE(EXTI15_10_IRQHandler = _default_handler);
PROVIDE(RTCAlarm_IRQHandler = _default_handler);
PROVIDE(USBWakeUp_IRQHandler = _default_handler);
PROVIDE(TIM8_BRK_IRQHandler = _default_handler);
PROVIDE(TIM8_UP_IRQHandler = _default_handler);
PROVIDE(TIM8_TRG_COM_IRQHandler = _default_handler);
PROVIDE(TIM8_CC_IRQHandler = _default_handler);
PROVIDE(ADC3_IRQHandler = _default_handler);
PROVIDE(FSMC_IRQHandler = _default_handler);
PROVIDE(SDIO_IRQHandler = _default_handler);
PROVIDE(TIM5_IRQHandler = _default_handler);
PROVIDE(SPI3_IRQHandler = _default_handler);
PROVIDE(UART4_IRQHandler = _default_handler);
PROVIDE(UART5_IRQHandler = _default_handler);
PROVIDE(TIM6_IRQHandler = _default_handler);
PROVIDE(TIM7_IRQHandler = _default_handler);
PROVIDE(DMA2_Channel1_IRQHandler = _default_handler);
PROVIDE(DMA2_Channel2_IRQHandler = _default_handler);
PROVIDE(DMA2_Channel3_IRQHandler = _default_handler);
PROVIDE(DMA2_Channel4_5_IRQHandler = _default_handler);

EXTERN(g_pfnVectors _default_handler);

/*========== end of file ==========*/
