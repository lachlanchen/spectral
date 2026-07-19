#ifndef AGINTI_C12880_BOARD_H
#define AGINTI_C12880_BOARD_H

#include "aginti/c12880.h"
#include "stm32h7xx_hal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define C12880_ST_PORT GPIOE
#define C12880_ST_PIN GPIO_PIN_2
#define C12880_CLK_PORT GPIOE
#define C12880_CLK_PIN GPIO_PIN_3
#define C12880_ADC_PORT GPIOA
#define C12880_ADC_PIN GPIO_PIN_0
#define C12880_TRIGGER_PORT GPIOE
#define C12880_TRIGGER_PIN GPIO_PIN_9
#define C12880_MODE_PORT GPIOE
#define C12880_MODE0_PIN GPIO_PIN_13
#define C12880_MODE1_PIN GPIO_PIN_14
#define C12880_EEPROM_SCL_PORT GPIOB
#define C12880_EEPROM_SCL_PIN GPIO_PIN_6
#define C12880_EEPROM_SDA_PORT GPIOB
#define C12880_EEPROM_SDA_PIN GPIO_PIN_7

extern ADC_HandleTypeDef hadc1;
extern DMA_HandleTypeDef hdma_adc1;
extern PCD_HandleTypeDef hpcd_USB_OTG_HS;

void board_mpu_config(void);
bool board_clock_config(void);
bool board_peripherals_init(void);
aginti_capture_start_result_t board_capture(
    void *context, uint16_t *raw, const aginti_c12880_config_t *config,
    uint32_t *status);
void board_capture_abort(void *context);
uint64_t board_time_us(void *context);
uint32_t board_critical_enter(void *context);
void board_critical_exit(void *context, uint32_t token);
bool board_external_trigger_take(void);
bool board_eeprom_read(uint16_t address, uint8_t *data, size_t bytes);
void board_safe_outputs(void);
void board_panic(uint32_t code) __attribute__((noreturn));

#endif

