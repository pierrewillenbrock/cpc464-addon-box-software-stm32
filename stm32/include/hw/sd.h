
#pragma once

#define SD_PWR_PIN GPIO_Pin_8
#define SD_PWR_GPIO GPIOA
#define SD_CD_PIN GPIO_Pin_5
#define SD_CD_GPIO GPIOB
#define SD_CD_EXTI_Line EXTI_Line5
#define SD_CD_EXTI_PortSourceGPIO EXTI_PortSourceGPIOB
#define SD_CD_EXTI_PinSource EXTI_PinSource5

#define SD_PWR_CD_GPIO_RCC (RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_GPIOB)

#define SD_CLK_PIN GPIO_Pin_12
#define SD_CLK_GPIO GPIOC
#define SD_PINS1 (GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11)
#define SD_GPIO1 GPIOC
#define SD_PINS2 (GPIO_Pin_2)
#define SD_GPIO2 GPIOD

#define SD_GPIO_RCC (RCC_AHB1Periph_GPIOC | RCC_AHB1Periph_GPIOD)


