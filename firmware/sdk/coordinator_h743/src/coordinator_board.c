#include "coordinator_board.h"

#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_uart.h"

#include <stdio.h>
#include <string.h>

#define COORD_PWM_HZ 400U
#define HOST_BAUD 115200U
#define SYNC_BAUD 1000000U
#define BB_I2C_HALF_US 5U
#define INA1_ADDR 0x41U
#define INA2_ADDR 0x40U
#define TSL2591_ADDR 0x29U
#define AS7343_ADDR 0x39U
#define TSL_CMD 0xA0U
#define AS_CFG0 0xBFU
#define AS_ENABLE 0x80U
#define AS_ATIME 0x81U
#define AS_STATUS2 0x90U
#define AS_ID 0x5AU
#define AS_DATA0 0x95U
#define AS_CFG1 0xC6U
#define AS_ASTEP_L 0xD4U
#define AS_CFG20 0xD6U

static TIM_HandleTypeDef htim2;
static TIM_HandleTypeDef htim5;
static UART_HandleTypeDef huart1;
static UART_HandleTypeDef huart3;
static uint32_t pwm_period_ticks;
static volatile bool pwm_pending;
static volatile bool pwm_commit_ready;
static volatile uint16_t staged_duty1;
static volatile uint16_t staged_duty2;
static volatile uint32_t pwm_commit_timestamp;
static uint32_t probe_flags;

static void delay_us(uint32_t us) {
  const uint32_t ticks = (SystemCoreClock / 1000000U) * us;
  const uint32_t start = DWT->CYCCNT;
  while ((uint32_t)(DWT->CYCCNT - start) < ticks) __NOP();
}

static bool clock_config(void) {
  RCC_OscInitTypeDef oscillator = {0};
  RCC_ClkInitTypeDef clock = {0};
  if (HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY) != HAL_OK) return false;
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}
  oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  oscillator.HSEState = RCC_HSE_ON;
  oscillator.PLL.PLLState = RCC_PLL_ON;
  oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  oscillator.PLL.PLLM = 5U;
  oscillator.PLL.PLLN = 160U;
  oscillator.PLL.PLLP = 2U;
  oscillator.PLL.PLLQ = 4U;
  oscillator.PLL.PLLR = 2U;
  oscillator.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  oscillator.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  if (HAL_RCC_OscConfig(&oscillator) != HAL_OK) return false;
  clock.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_PCLK1 |
                    RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1;
  clock.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clock.SYSCLKDivider = RCC_SYSCLK_DIV1;
  clock.AHBCLKDivider = RCC_HCLK_DIV2;
  clock.APB3CLKDivider = RCC_APB3_DIV2;
  clock.APB1CLKDivider = RCC_APB1_DIV2;
  clock.APB2CLKDivider = RCC_APB2_DIV2;
  clock.APB4CLKDivider = RCC_APB4_DIV2;
  if (HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_4) != HAL_OK) return false;
  SystemCoreClockUpdate();
  HAL_SYSTICK_Config(SystemCoreClock / 1000U);
  HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);
  return true;
}

static void gpio_init(void) {
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = COORD_LAMP1_PIN | COORD_LAMP2_PIN;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_PULLDOWN;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = GPIO_AF1_TIM2;
  HAL_GPIO_Init(GPIOA, &gpio);
  gpio.Pin = COORD_TRIGGER_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_PULLDOWN;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(COORD_TRIGGER_PORT, &gpio);
  HAL_GPIO_WritePin(COORD_TRIGGER_PORT, COORD_TRIGGER_PIN, GPIO_PIN_RESET);
  gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Alternate = GPIO_AF7_USART1;
  HAL_GPIO_Init(GPIOA, &gpio);
  gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;
  gpio.Alternate = GPIO_AF7_USART3;
  HAL_GPIO_Init(GPIOD, &gpio);
  gpio.Pin = COORD_I2C_SCL_PIN | COORD_I2C_SDA_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_MEDIUM;
  gpio.Alternate = 0U;
  HAL_GPIO_Init(COORD_I2C_PORT, &gpio);
  HAL_GPIO_WritePin(COORD_I2C_PORT,
                    COORD_I2C_SCL_PIN | COORD_I2C_SDA_PIN, GPIO_PIN_SET);
}

static bool timebase_init(void) {
  __HAL_RCC_TIM5_CLK_ENABLE();
  const uint32_t pclk = HAL_RCC_GetPCLK1Freq();
  const uint32_t timer_hz = ((RCC->D2CFGR & RCC_D2CFGR_D2PPRE1) == 0U)
                                ? pclk : pclk * 2U;
  htim5.Instance = TIM5;
  htim5.Init.Prescaler = (timer_hz / 1000000U) - 1U;
  htim5.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim5.Init.Period = 0xFFFFFFFFU;
  htim5.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim5.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  return HAL_TIM_Base_Init(&htim5) == HAL_OK &&
         HAL_TIM_Base_Start(&htim5) == HAL_OK;
}

static bool pwm_init(void) {
  __HAL_RCC_TIM2_CLK_ENABLE();
  const uint32_t pclk = HAL_RCC_GetPCLK1Freq();
  const uint32_t timer_hz = ((RCC->D2CFGR & RCC_D2CFGR_D2PPRE1) == 0U)
                                ? pclk : pclk * 2U;
  pwm_period_ticks = timer_hz / COORD_PWM_HZ;
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0U;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = pwm_period_ticks - 1U;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) return false;
  TIM_OC_InitTypeDef output = {0};
  output.OCMode = TIM_OCMODE_PWM1;
  output.Pulse = 0U;
  output.OCPolarity = TIM_OCPOLARITY_HIGH;
  output.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &output, TIM_CHANNEL_1) != HAL_OK ||
      HAL_TIM_PWM_ConfigChannel(&htim2, &output, TIM_CHANNEL_2) != HAL_OK)
    return false;
  TIM2->CCMR1 |= TIM_CCMR1_OC1PE | TIM_CCMR1_OC2PE;
  TIM2->CCR1 = 0U;
  TIM2->CCR2 = 0U;
  TIM2->EGR = TIM_EGR_UG;
  __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);
  HAL_NVIC_SetPriority(TIM2_IRQn, 3U, 0U);
  HAL_NVIC_EnableIRQ(TIM2_IRQn);
  __HAL_TIM_ENABLE_IT(&htim2, TIM_IT_UPDATE);
  return HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1) == HAL_OK &&
         HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2) == HAL_OK;
}

static bool uart_init(void) {
  __HAL_RCC_USART1_CLK_ENABLE();
  __HAL_RCC_USART3_CLK_ENABLE();
  huart1.Instance = USART1;
  huart1.Init.BaudRate = HOST_BAUD;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  huart3 = huart1;
  huart3.Instance = USART3;
  huart3.Init.BaudRate = SYNC_BAUD;
  return HAL_UART_Init(&huart1) == HAL_OK && HAL_UART_Init(&huart3) == HAL_OK;
}

static void bb_delay(void) { delay_us(BB_I2C_HALF_US); }
static void bb_scl(bool high) {
  HAL_GPIO_WritePin(COORD_I2C_PORT, COORD_I2C_SCL_PIN,
                   high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
static void bb_sda(bool high) {
  HAL_GPIO_WritePin(COORD_I2C_PORT, COORD_I2C_SDA_PIN,
                   high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
static bool bb_sda_read(void) {
  return HAL_GPIO_ReadPin(COORD_I2C_PORT, COORD_I2C_SDA_PIN) == GPIO_PIN_SET;
}
static void bb_start(void) {
  bb_sda(true); bb_scl(true); bb_delay(); bb_sda(false); bb_delay();
  bb_scl(false); bb_delay();
}
static void bb_stop(void) {
  bb_sda(false); bb_delay(); bb_scl(true); bb_delay(); bb_sda(true); bb_delay();
}
static bool bb_write(uint8_t value) {
  for (uint32_t i = 0U; i < 8U; ++i) {
    bb_sda((value & 0x80U) != 0U); bb_delay(); bb_scl(true); bb_delay();
    bb_scl(false); value <<= 1;
  }
  bb_sda(true); bb_delay(); bb_scl(true); bb_delay();
  const bool ack = !bb_sda_read();
  bb_scl(false); bb_delay();
  return ack;
}
static uint8_t bb_read(bool ack) {
  uint8_t value = 0U;
  bb_sda(true);
  for (uint32_t i = 0U; i < 8U; ++i) {
    value <<= 1; bb_scl(true); bb_delay();
    if (bb_sda_read()) value |= 1U;
    bb_scl(false); bb_delay();
  }
  bb_sda(!ack); bb_scl(true); bb_delay(); bb_scl(false); bb_sda(true); bb_delay();
  return value;
}
static bool i2c_present(uint8_t address) {
  bb_start(); const bool ok = bb_write((uint8_t)(address << 1)); bb_stop();
  return ok;
}
static bool i2c_write8(uint8_t address, uint8_t reg, uint8_t value) {
  bb_start();
  const bool ok = bb_write((uint8_t)(address << 1)) && bb_write(reg) &&
                  bb_write(value);
  bb_stop(); return ok;
}
static bool i2c_write16be(uint8_t address, uint8_t reg, uint16_t value) {
  bb_start();
  const bool ok = bb_write((uint8_t)(address << 1)) && bb_write(reg) &&
                  bb_write((uint8_t)(value >> 8)) && bb_write((uint8_t)value);
  bb_stop(); return ok;
}
static bool i2c_write16le(uint8_t address, uint8_t reg, uint16_t value) {
  bb_start();
  const bool ok = bb_write((uint8_t)(address << 1)) && bb_write(reg) &&
                  bb_write((uint8_t)value) && bb_write((uint8_t)(value >> 8));
  bb_stop(); return ok;
}
static bool i2c_read_bytes(uint8_t address, uint8_t reg, uint8_t *data,
                           uint32_t count) {
  bb_start();
  if (!bb_write((uint8_t)(address << 1)) || !bb_write(reg)) { bb_stop(); return false; }
  bb_start();
  if (!bb_write((uint8_t)((address << 1) | 1U))) { bb_stop(); return false; }
  for (uint32_t i = 0U; i < count; ++i) data[i] = bb_read(i + 1U < count);
  bb_stop(); return true;
}
static bool i2c_read16be(uint8_t address, uint8_t reg, uint16_t *value) {
  uint8_t data[2];
  if (!i2c_read_bytes(address, reg, data, 2U)) return false;
  *value = (uint16_t)(((uint16_t)data[0] << 8) | data[1]); return true;
}

static bool as_bank(bool bank1) {
  uint8_t value = 0U;
  if (!i2c_read_bytes(AS7343_ADDR, AS_CFG0, &value, 1U)) return false;
  value = bank1 ? (uint8_t)(value | 0x10U) : (uint8_t)(value & ~0x10U);
  return i2c_write8(AS7343_ADDR, AS_CFG0, value);
}
static bool as_configure(void) {
  uint8_t id = 0U, cfg20 = 0U;
  if (!as_bank(true) || !i2c_read_bytes(AS7343_ADDR, AS_ID, &id, 1U) ||
      id != 0x81U || !as_bank(false)) return false;
  if (!i2c_write8(AS7343_ADDR, AS_ENABLE, 0x01U)) return false;
  HAL_Delay(10U);
  if (!i2c_write8(AS7343_ADDR, AS_ATIME, 5U) ||
      !i2c_write16le(AS7343_ADDR, AS_ASTEP_L, 599U) ||
      !i2c_write8(AS7343_ADDR, AS_CFG1, 0U) ||
      !i2c_read_bytes(AS7343_ADDR, AS_CFG20, &cfg20, 1U)) return false;
  cfg20 = (uint8_t)((cfg20 & ~0x60U) | 0x60U);
  return i2c_write8(AS7343_ADDR, AS_CFG20, cfg20) &&
         i2c_write8(AS7343_ADDR, AS_ENABLE, 0x03U);
}
static bool tsl_configure(void) {
  return i2c_write8(TSL2591_ADDR, TSL_CMD | 0x00U, 0x03U) &&
         i2c_write8(TSL2591_ADDR, TSL_CMD | 0x01U, 0x00U);
}
static bool ina_configure(uint8_t address) {
  return i2c_write16be(address, 0x00U, 0x399FU);
}

bool coord_board_init(void) {
  SCB_EnableICache();
  SCB_EnableDCache();
  HAL_Init();
  if (!clock_config()) return false;
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  gpio_init();
  if (!timebase_init() || !pwm_init() || !uart_init()) return false;
  probe_flags = 0U;
  if (i2c_present(INA1_ADDR) && ina_configure(INA1_ADDR)) probe_flags |= AGINTI_TELEM_INA1;
  if (i2c_present(INA2_ADDR) && ina_configure(INA2_ADDR)) probe_flags |= AGINTI_TELEM_INA2;
  if (i2c_present(TSL2591_ADDR) && tsl_configure()) probe_flags |= AGINTI_TELEM_TSL2591;
  if (i2c_present(AS7343_ADDR) && as_configure()) probe_flags |= AGINTI_TELEM_AS7343;
  coord_board_force_off();
  return true;
}

uint32_t coord_board_now_us(void) { return TIM5->CNT; }
uint32_t coord_board_pwm_period_us(void) { return 1000000U / COORD_PWM_HZ; }

bool coord_board_pwm_stage(uint16_t duty1_q16, uint16_t duty2_q16) {
  const uint32_t compare1 = (uint32_t)(((uint64_t)duty1_q16 * pwm_period_ticks + 32767U) / 65535U);
  const uint32_t compare2 = (uint32_t)(((uint64_t)duty2_q16 * pwm_period_ticks + 32767U) / 65535U);
  TIM2->CR1 |= TIM_CR1_UDIS;
  TIM2->CCR1 = compare1;
  TIM2->CCR2 = compare2;
  staged_duty1 = duty1_q16;
  staged_duty2 = duty2_q16;
  pwm_pending = true;
  pwm_commit_ready = false;
  TIM2->CR1 &= ~TIM_CR1_UDIS;
  return true;
}

bool coord_board_pwm_take_commit(uint32_t *timestamp_us) {
  const uint32_t mask = __get_PRIMASK();
  __disable_irq();
  const bool ready = pwm_commit_ready;
  if (ready) {
    *timestamp_us = pwm_commit_timestamp;
    pwm_commit_ready = false;
  }
  if (mask == 0U) __enable_irq();
  return ready;
}

void coord_board_force_off(void) {
  const uint32_t mask = __get_PRIMASK();
  __disable_irq();
  TIM2->CR1 &= ~TIM_CR1_UDIS;
  TIM2->CCR1 = 0U;
  TIM2->CCR2 = 0U;
  TIM2->EGR = TIM_EGR_UG;
  pwm_pending = false;
  pwm_commit_ready = false;
  staged_duty1 = 0U;
  staged_duty2 = 0U;
  HAL_GPIO_WritePin(COORD_TRIGGER_PORT, COORD_TRIGGER_PIN, GPIO_PIN_RESET);
  if (mask == 0U) __enable_irq();
}

void TIM2_IRQHandler(void) {
  if ((TIM2->SR & TIM_SR_UIF) != 0U) {
    TIM2->SR = (uint32_t)~TIM_SR_UIF;
    if (pwm_pending) {
      (void)staged_duty1;
      (void)staged_duty2;
      pwm_commit_timestamp = coord_board_now_us();
      pwm_pending = false;
      pwm_commit_ready = true;
    }
  }
}

void coord_board_trigger_pulse(uint32_t width_us) {
  HAL_GPIO_WritePin(COORD_TRIGGER_PORT, COORD_TRIGGER_PIN, GPIO_PIN_SET);
  delay_us(width_us);
  HAL_GPIO_WritePin(COORD_TRIGGER_PORT, COORD_TRIGGER_PIN, GPIO_PIN_RESET);
}

static uint16_t crc16(const uint8_t *data, size_t bytes) {
  uint16_t crc = 0xFFFFU;
  for (size_t i = 0U; i < bytes; ++i) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint32_t bit = 0U; bit < 8U; ++bit)
      crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U)
                            : (uint16_t)(crc << 1);
  }
  return crc;
}

void coord_board_send_sync(uint32_t sequence, uint32_t timestamp_us,
                           uint16_t lut_index, uint16_t duty1_q16,
                           uint16_t duty2_q16) {
  aginti_coord_sync_packet_t packet = {0};
  packet.magic = COORD_SYNC_MAGIC;
  packet.version = COORD_SYNC_VERSION;
  packet.sequence = sequence;
  packet.timestamp_us = timestamp_us;
  packet.lut_index = lut_index;
  packet.duty1_q16 = duty1_q16;
  packet.duty2_q16 = duty2_q16;
  packet.crc16 = crc16((const uint8_t *)&packet,
                      sizeof(packet) - sizeof(packet.crc16));
  (void)HAL_UART_Transmit(&huart3, (const uint8_t *)&packet,
                          (uint16_t)sizeof(packet), 5U);
}

static bool read_ina(uint8_t address, uint16_t *bus_mv, uint16_t *current_ma,
                     uint32_t *power_mw) {
  uint16_t shunt_raw_u = 0U, bus_raw = 0U;
  if (!i2c_read16be(address, 0x01U, &shunt_raw_u) ||
      !i2c_read16be(address, 0x02U, &bus_raw)) return false;
  const int32_t shunt_raw = (int16_t)shunt_raw_u;
  const uint32_t magnitude = (uint32_t)((shunt_raw < 0) ? -shunt_raw : shunt_raw);
  *bus_mv = (uint16_t)((bus_raw >> 3) * 4U);
  *current_ma = (uint16_t)((magnitude * 10U + 50U) / 100U);
  *power_mw = ((uint32_t)*bus_mv * *current_ma + 500U) / 1000U;
  return true;
}

bool coord_board_read_telemetry(aginti_coord_telemetry_t *telemetry) {
  memset(telemetry, 0, sizeof(*telemetry));
  telemetry->timestamp_us = coord_board_now_us();
  if ((probe_flags & AGINTI_TELEM_INA1) != 0U &&
      read_ina(INA1_ADDR, &telemetry->bus1_mv, &telemetry->current1_ma,
               &telemetry->power1_mw)) telemetry->valid_flags |= AGINTI_TELEM_INA1;
  if ((probe_flags & AGINTI_TELEM_INA2) != 0U &&
      read_ina(INA2_ADDR, &telemetry->bus2_mv, &telemetry->current2_ma,
               &telemetry->power2_mw)) telemetry->valid_flags |= AGINTI_TELEM_INA2;
  if ((probe_flags & AGINTI_TELEM_TSL2591) != 0U) {
    uint8_t raw[4];
    if (i2c_read_bytes(TSL2591_ADDR, TSL_CMD | 0x14U, raw, 4U)) {
      telemetry->tsl_ch0 = (uint16_t)(((uint16_t)raw[1] << 8) | raw[0]);
      telemetry->tsl_ch1 = (uint16_t)(((uint16_t)raw[3] << 8) | raw[2]);
      telemetry->valid_flags |= AGINTI_TELEM_TSL2591;
    }
  }
  if ((probe_flags & AGINTI_TELEM_AS7343) != 0U && as_bank(false)) {
    uint8_t status = 0U, raw[36];
    if (i2c_read_bytes(AS7343_ADDR, AS_STATUS2, &status, 1U) &&
        (status & 0x40U) != 0U &&
        i2c_read_bytes(AS7343_ADDR, AS_DATA0, raw, sizeof(raw))) {
      for (uint32_t i = 0U; i < 18U; ++i)
        telemetry->as7343[i] = (uint16_t)(((uint16_t)raw[2U * i + 1U] << 8) |
                                          raw[2U * i]);
      telemetry->valid_flags |= AGINTI_TELEM_AS7343;
    }
  }
  return telemetry->valid_flags != 0U;
}

uint32_t coord_board_probe_flags(void) { return probe_flags; }

bool coord_board_host_read_byte(uint8_t *value) {
  if (HAL_UART_Receive(&huart1, value, 1U, 0U) == HAL_OK) return true;
  if (huart1.ErrorCode != HAL_UART_ERROR_NONE) {
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    huart1.ErrorCode = HAL_UART_ERROR_NONE;
    huart1.RxState = HAL_UART_STATE_READY;
  }
  return false;
}

void coord_board_host_write(const char *text) {
  const size_t bytes = strlen(text);
  if (bytes > 0U)
    (void)HAL_UART_Transmit(&huart1, (const uint8_t *)text,
                            (uint16_t)bytes, 100U);
}

void coord_board_panic(uint32_t code) {
  (void)code;
  coord_board_force_off();
  __disable_irq();
  while (1) __WFI();
}
