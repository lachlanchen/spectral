#ifndef STM32H7XX_HAL_CONF_H
#define STM32H7XX_HAL_CONF_H

#define HAL_MODULE_ENABLED
#define HAL_ADC_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_PCD_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED

#ifndef HSE_VALUE
#define HSE_VALUE 25000000UL
#endif
#define HSE_STARTUP_TIMEOUT 100UL
#define LSE_STARTUP_TIMEOUT 5000UL
#define CSI_VALUE 4000000UL
#define HSI_VALUE 64000000UL
#define LSI_VALUE 32000UL
#define LSE_VALUE 32768UL
#define EXTERNAL_CLOCK_VALUE 12288000UL
#define VDD_VALUE 3300UL
#define TICK_INT_PRIORITY 15UL
#define USE_RTOS 0U
#define USE_SD_TRANSCEIVER 0U
#define USE_SPI_CRC 0U
#define USE_HAL_ADC_REGISTER_CALLBACKS 0U
#define USE_HAL_PCD_REGISTER_CALLBACKS 0U
#define USE_FULL_ASSERT 1U

#include "stm32h7xx_hal_rcc.h"
#include "stm32h7xx_hal_gpio.h"
#include "stm32h7xx_hal_dma.h"
#include "stm32h7xx_hal_cortex.h"
#include "stm32h7xx_hal_adc.h"
#include "stm32h7xx_hal_exti.h"
#include "stm32h7xx_hal_flash.h"
#include "stm32h7xx_hal_pwr.h"
#include "stm32h7xx_hal_pcd.h"
#include "stm32h7xx_hal_tim.h"

void assert_failed(uint8_t *file, uint32_t line);
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))

#endif
