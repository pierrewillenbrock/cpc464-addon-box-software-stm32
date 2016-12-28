
#pragma once

#include <bsp/stm32f4xx.h>

static inline uint32_t interrupt_disable() {
	uint32_t level = __get_BASEPRI();
	__set_BASEPRI(0x40);
	return level;
}

static inline void interrupt_enable(uint32_t level) {
	__set_BASEPRI(level);
}

#define ISR_Disable( cookie ) do { cookie = interrupt_disable(); } while(0)
#define ISR_Enable( cookie ) interrupt_enable(cookie)

#ifdef __cplusplus
class ISR_Guard {
private:
	uint32_t level;
public:
	ISR_Guard() { ISR_Disable(level); }
	~ISR_Guard() { ISR_Enable(level); }
};
#endif

#ifdef __cplusplus
 extern "C" {
#endif

void Reset_Handler(void) __attribute__((interrupt));
void NMI_Handler(void) __attribute__((interrupt));
void HardFault_Handler(void) __attribute__((interrupt));
void MemManage_Handler(void) __attribute__((interrupt));
void BusFault_Handler(void) __attribute__((interrupt));
void UsageFault_Handler(void) __attribute__((interrupt));
void SVC_Handler(void) __attribute__((interrupt));
void DebugMon_Handler(void) __attribute__((interrupt));
void PendSV_Handler(void) __attribute__((interrupt));
void SysTick_Handler(void) __attribute__((interrupt));
void WWDG_IRQHandler(void) __attribute__((interrupt));
void PVD_IRQHandler(void) __attribute__((interrupt));
void TAMP_STAMP_IRQHandler(void) __attribute__((interrupt));
void RTC_WKUP_IRQHandler(void) __attribute__((interrupt));
void FLASH_IRQHandler(void) __attribute__((interrupt));
void RCC_IRQHandler(void) __attribute__((interrupt));
void EXTI0_IRQHandler(void) __attribute__((interrupt));
void EXTI1_IRQHandler(void) __attribute__((interrupt));
void EXTI2_IRQHandler(void) __attribute__((interrupt));
void EXTI3_IRQHandler(void) __attribute__((interrupt));
void EXTI4_IRQHandler(void) __attribute__((interrupt));
void DMA1_Stream0_IRQHandler(void) __attribute__((interrupt));
void DMA1_Stream1_IRQHandler(void) __attribute__((interrupt));
void DMA1_Stream2_IRQHandler(void) __attribute__((interrupt));
void DMA1_Stream3_IRQHandler(void) __attribute__((interrupt));
void DMA1_Stream4_IRQHandler(void) __attribute__((interrupt));
void DMA1_Stream5_IRQHandler(void) __attribute__((interrupt));
void DMA1_Stream6_IRQHandler(void) __attribute__((interrupt));
void ADC_IRQHandler(void) __attribute__((interrupt));
void CAN1_TX_IRQHandler(void) __attribute__((interrupt));
void CAN1_RX0_IRQHandler(void) __attribute__((interrupt));
void CAN1_RX1_IRQHandler(void) __attribute__((interrupt));
void CAN1_SCE_IRQHandler(void) __attribute__((interrupt));
void EXTI9_5_IRQHandler(void) __attribute__((interrupt));
void TIM1_BRK_TIM9_IRQHandler(void) __attribute__((interrupt));
void TIM1_UP_TIM10_IRQHandler(void) __attribute__((interrupt));
void TIM1_TRG_COM_TIM11_IRQHandler(void) __attribute__((interrupt));
void TIM1_CC_IRQHandler(void) __attribute__((interrupt));
void TIM2_IRQHandler(void) __attribute__((interrupt));
void TIM3_IRQHandler(void) __attribute__((interrupt));
void TIM4_IRQHandler(void) __attribute__((interrupt));
void I2C1_EV_IRQHandler(void) __attribute__((interrupt));
void I2C1_ER_IRQHandler(void) __attribute__((interrupt));
void I2C2_EV_IRQHandler(void) __attribute__((interrupt));
void I2C2_ER_IRQHandler(void) __attribute__((interrupt));
void SPI1_IRQHandler(void) __attribute__((interrupt));
void SPI2_IRQHandler(void) __attribute__((interrupt));
void USART1_IRQHandler(void) __attribute__((interrupt));
void USART2_IRQHandler(void) __attribute__((interrupt));
void USART3_IRQHandler(void) __attribute__((interrupt));
void EXTI15_10_IRQHandler(void) __attribute__((interrupt));
void RTC_Alarm_IRQHandler(void) __attribute__((interrupt));
void OTG_FS_WKUP_IRQHandler(void) __attribute__((interrupt));
void TIM8_BRK_TIM12_IRQHandler(void) __attribute__((interrupt));
void TIM8_UP_TIM13_IRQHandler(void) __attribute__((interrupt));
void TIM8_TRG_COM_TIM14_IRQHandler(void) __attribute__((interrupt));
void TIM8_CC_IRQHandler(void) __attribute__((interrupt));
void DMA1_Stream7_IRQHandler(void) __attribute__((interrupt));
void FSMC_IRQHandler(void) __attribute__((interrupt));
void SDIO_IRQHandler(void) __attribute__((interrupt));
void TIM5_IRQHandler(void) __attribute__((interrupt));
void SPI3_IRQHandler(void) __attribute__((interrupt));
void UART4_IRQHandler(void) __attribute__((interrupt));
void UART5_IRQHandler(void) __attribute__((interrupt));
void TIM6_DAC_IRQHandler(void) __attribute__((interrupt));
void TIM7_IRQHandler(void) __attribute__((interrupt));
void DMA2_Stream0_IRQHandler(void) __attribute__((interrupt));
void DMA2_Stream1_IRQHandler(void) __attribute__((interrupt));
void DMA2_Stream2_IRQHandler(void) __attribute__((interrupt));
void DMA2_Stream3_IRQHandler(void) __attribute__((interrupt));
void DMA2_Stream4_IRQHandler(void) __attribute__((interrupt));
void ETH_IRQHandler(void) __attribute__((interrupt));
void ETH_WKUP_IRQHandler(void) __attribute__((interrupt));
void CAN2_TX_IRQHandler(void) __attribute__((interrupt));
void CAN2_RX0_IRQHandler(void) __attribute__((interrupt));
void CAN2_RX1_IRQHandler(void) __attribute__((interrupt));
void CAN2_SCE_IRQHandler(void) __attribute__((interrupt));
void OTG_FS_IRQHandler(void) __attribute__((interrupt));
void DMA2_Stream5_IRQHandler(void) __attribute__((interrupt));
void DMA2_Stream6_IRQHandler(void) __attribute__((interrupt));
void DMA2_Stream7_IRQHandler(void) __attribute__((interrupt));
void USART6_IRQHandler(void) __attribute__((interrupt));
void I2C3_EV_IRQHandler(void) __attribute__((interrupt));
void I2C3_ER_IRQHandler(void) __attribute__((interrupt));
void OTG_HS_EP1_OUT_IRQHandler(void) __attribute__((interrupt));
void OTG_HS_EP1_IN_IRQHandler(void) __attribute__((interrupt));
void OTG_HS_WKUP_IRQHandler(void) __attribute__((interrupt));
void OTG_HS_IRQHandler(void) __attribute__((interrupt));
void DCMI_IRQHandler(void) __attribute__((interrupt));
void CRYP_IRQHandler(void) __attribute__((interrupt));
void HASH_RNG_IRQHandler(void) __attribute__((interrupt));
void FPU_IRQHandler(void) __attribute__((interrupt));

#ifdef __cplusplus
 }
#endif
