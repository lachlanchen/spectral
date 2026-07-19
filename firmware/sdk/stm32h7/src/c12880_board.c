#include "c12880_board.h"

#include <string.h>

#ifndef AGINTI_CAPTURE_DMA
#define AGINTI_CAPTURE_DMA 1
#endif

#define C12880_PRE_CLOCKS 12U
#define C12880_DUMMY_CLOCKS_DIRECT 91U
#define C12880_DUMMY_CLOCKS_DMA 90U
#define EEPROM_DEVICE_WRITE_ADDRESS 0xA0U
#define EEPROM_DEVICE_READ_ADDRESS 0xA1U
#define EEPROM_HALF_PERIOD_CYCLES (SystemCoreClock / 200000U)

ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;
#if AGINTI_CAPTURE_DMA
static TIM_HandleTypeDef htim2;
static DMA_HandleTypeDef hdma_clk_set;
static DMA_HandleTypeDef hdma_clk_reset;
static uint32_t timer_input_hz;
static uint32_t active_sensor_clock_hz;
__attribute__((section(".dma_buffer"), aligned(32)))
static uint32_t clk_set_word = (uint32_t)C12880_CLK_PIN;
__attribute__((section(".dma_buffer"), aligned(32)))
static uint32_t clk_reset_word = ((uint32_t)C12880_CLK_PIN << 16);
#endif
static volatile bool external_trigger_pending;

static inline void gpio_set(GPIO_TypeDef *port, uint16_t pin) {
  port->BSRR = (uint32_t)pin;
}

static inline void gpio_reset(GPIO_TypeDef *port, uint16_t pin) {
  port->BSRR = (uint32_t)pin << 16;
}

static void dwt_delay(uint32_t cycles) {
  const uint32_t start = DWT->CYCCNT;
  while ((uint32_t)(DWT->CYCCNT - start) < cycles) {
    __NOP();
  }
}

void board_mpu_config(void) {
  MPU_Region_InitTypeDef region = {0};
  HAL_MPU_Disable();
  region.Enable = MPU_REGION_ENABLE;
  region.Number = MPU_REGION_NUMBER0;
  region.BaseAddress = 0x30000000U;
  region.Size = MPU_REGION_SIZE_256KB;
  region.SubRegionDisable = 0x00U;
  region.TypeExtField = MPU_TEX_LEVEL0;
  region.AccessPermission = MPU_REGION_FULL_ACCESS;
  region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  region.IsShareable = MPU_ACCESS_SHAREABLE;
  region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  HAL_MPU_ConfigRegion(&region);
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

bool board_clock_config(void) {
  RCC_OscInitTypeDef oscillator = {0};
  RCC_ClkInitTypeDef clock = {0};
  RCC_PeriphCLKInitTypeDef peripheral = {0};

  if (HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY) != HAL_OK) return false;
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSE |
                              RCC_OSCILLATORTYPE_HSI48;
  oscillator.HSEState = RCC_HSE_ON;
  oscillator.HSI48State = RCC_HSI48_ON;
  oscillator.PLL.PLLState = RCC_PLL_ON;
  oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  oscillator.PLL.PLLM = 5U;
  oscillator.PLL.PLLN = 160U;
  oscillator.PLL.PLLP = 2U;
  oscillator.PLL.PLLQ = 4U;
  oscillator.PLL.PLLR = 2U;
  oscillator.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  oscillator.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  oscillator.PLL.PLLFRACN = 0U;
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

  peripheral.PeriphClockSelection = RCC_PERIPHCLK_USB | RCC_PERIPHCLK_ADC;
  peripheral.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
  peripheral.AdcClockSelection = RCC_ADCCLKSOURCE_PLL2;
  peripheral.PLL2.PLL2M = 5U;
  peripheral.PLL2.PLL2N = 40U;
  peripheral.PLL2.PLL2P = 2U;
  peripheral.PLL2.PLL2Q = 4U;
  peripheral.PLL2.PLL2R = 4U;
  peripheral.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_2;
  peripheral.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
  peripheral.PLL2.PLL2FRACN = 0U;
  if (HAL_RCCEx_PeriphCLKConfig(&peripheral) != HAL_OK) return false;

  SystemCoreClockUpdate();
  HAL_SYSTICK_Config(SystemCoreClock / 1000U);
  HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);
  return true;
}

static void gpio_init(void) {
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();

  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = C12880_ST_PIN | C12880_CLK_PIN |
             C12880_MODE0_PIN | C12880_MODE1_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOE, &gpio);

  gpio.Pin = C12880_TRIGGER_PIN;
  gpio.Mode = GPIO_MODE_IT_RISING;
  gpio.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(C12880_TRIGGER_PORT, &gpio);

  gpio.Pin = C12880_EEPROM_SCL_PIN | C12880_EEPROM_SDA_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &gpio);
  gpio_set(C12880_EEPROM_SCL_PORT, C12880_EEPROM_SCL_PIN);
  gpio_set(C12880_EEPROM_SDA_PORT, C12880_EEPROM_SDA_PIN);

  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 7U, 0U);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
  board_safe_outputs();
}

static bool adc_dma_init(void) {
  __HAL_RCC_DMA1_CLK_ENABLE();
  __HAL_RCC_ADC12_CLK_ENABLE();

  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = C12880_ADC_PIN;
  gpio.Mode = GPIO_MODE_ANALOG;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(C12880_ADC_PORT, &gpio);

  hdma_adc1.Instance = DMA1_Stream0;
  hdma_adc1.Init.Request = DMA_REQUEST_ADC1;
  hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
  hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
  hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
  hdma_adc1.Init.Mode = DMA_NORMAL;
  hdma_adc1.Init.Priority = DMA_PRIORITY_VERY_HIGH;
  hdma_adc1.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
  if (HAL_DMA_Init(&hdma_adc1) != HAL_OK) return false;
  __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);

  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV2;
  hadc1.Init.Resolution = ADC_RESOLUTION_16B;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1U;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
#if AGINTI_CAPTURE_DMA
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIG_T2_TRGO;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_ONESHOT;
#else
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
#endif
  hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  hadc1.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) return false;

  ADC_ChannelConfTypeDef channel = {0};
  channel.Channel = ADC_CHANNEL_16;
  channel.Rank = ADC_REGULAR_RANK_1;
  channel.SamplingTime = ADC_SAMPLETIME_16CYCLES_5;
  channel.SingleDiff = ADC_SINGLE_ENDED;
  channel.OffsetNumber = ADC_OFFSET_NONE;
  channel.Offset = 0U;
  if (HAL_ADC_ConfigChannel(&hadc1, &channel) != HAL_OK) return false;
  if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET,
                                  ADC_SINGLE_ENDED) != HAL_OK) return false;

  HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 4U, 0U);
  HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
  return true;
}

#if AGINTI_CAPTURE_DMA
static bool timer_dma_init(void) {
  __HAL_RCC_TIM2_CLK_ENABLE();
  hdma_clk_set.Instance = DMA1_Stream1;
  hdma_clk_set.Init.Request = DMA_REQUEST_TIM2_UP;
  hdma_clk_set.Init.Direction = DMA_MEMORY_TO_PERIPH;
  hdma_clk_set.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_clk_set.Init.MemInc = DMA_MINC_DISABLE;
  hdma_clk_set.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
  hdma_clk_set.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
  hdma_clk_set.Init.Mode = DMA_NORMAL;
  hdma_clk_set.Init.Priority = DMA_PRIORITY_VERY_HIGH;
  hdma_clk_set.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
  if (HAL_DMA_Init(&hdma_clk_set) != HAL_OK) return false;

  hdma_clk_reset = hdma_clk_set;
  hdma_clk_reset.Instance = DMA1_Stream2;
  hdma_clk_reset.Init.Request = DMA_REQUEST_TIM2_CH1;
  if (HAL_DMA_Init(&hdma_clk_reset) != HAL_OK) return false;

  const uint32_t pclk = HAL_RCC_GetPCLK1Freq();
  timer_input_hz = ((RCC->D2CFGR & RCC_D2CFGR_D2PPRE1) == 0U)
                       ? pclk
                       : (pclk * 2U);
  active_sensor_clock_hz = 1000000U;
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0U;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = (timer_input_hz / active_sensor_clock_hz) - 1U;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) return false;

  TIM_OC_InitTypeDef output = {0};
  output.OCMode = TIM_OCMODE_PWM1;
  output.Pulse = (htim2.Init.Period + 1U) / 2U;
  output.OCPolarity = TIM_OCPOLARITY_HIGH;
  output.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &output, TIM_CHANNEL_1) != HAL_OK)
    return false;
  output.OCMode = TIM_OCMODE_PWM2;
  output.Pulse = ((htim2.Init.Period + 1U) * 3U) / 4U;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &output, TIM_CHANNEL_2) != HAL_OK)
    return false;

  TIM_MasterConfigTypeDef master = {0};
  master.MasterOutputTrigger = TIM_TRGO_OC2REF;
  master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &master) != HAL_OK)
    return false;
  TIM2->CCER |= TIM_CCER_CC1E | TIM_CCER_CC2E;
  return true;
}
#endif

bool board_peripherals_init(void) {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  gpio_init();
  if (!adc_dma_init()) return false;
#if AGINTI_CAPTURE_DMA
  if (!timer_dma_init()) return false;
#endif
  return true;
}

static void set_output_mode(uint8_t mode) {
  if ((mode & 1U) != 0U) gpio_set(C12880_MODE_PORT, C12880_MODE0_PIN);
  else gpio_reset(C12880_MODE_PORT, C12880_MODE0_PIN);
  if ((mode & 2U) != 0U) gpio_set(C12880_MODE_PORT, C12880_MODE1_PIN);
  else gpio_reset(C12880_MODE_PORT, C12880_MODE1_PIN);
}

#if !AGINTI_CAPTURE_DMA
static void direct_clock(uint32_t sensor_clock_hz) {
  const uint32_t half = SystemCoreClock / (sensor_clock_hz * 2U);
  gpio_set(C12880_CLK_PORT, C12880_CLK_PIN);
  dwt_delay(half);
  gpio_reset(C12880_CLK_PORT, C12880_CLK_PIN);
  dwt_delay(half);
}

static bool direct_capture(uint16_t *raw,
                           const aginti_c12880_config_t *config,
                           uint32_t *status) {
  for (uint32_t i = 0; i < C12880_PRE_CLOCKS; ++i)
    direct_clock(config->sensor_clock_hz);
  gpio_set(C12880_ST_PORT, C12880_ST_PIN);
  for (uint32_t i = 0; i < config->exposure_clocks; ++i)
    direct_clock(config->sensor_clock_hz);
  gpio_reset(C12880_ST_PORT, C12880_ST_PIN);
  for (uint32_t i = 0; i < C12880_DUMMY_CLOCKS_DIRECT; ++i)
    direct_clock(config->sensor_clock_hz);
  for (uint32_t i = 0; i < AGINTI_C12880_RAW_SAMPLES; ++i) {
    if ((HAL_ADC_Start(&hadc1) != HAL_OK) ||
        (HAL_ADC_PollForConversion(&hadc1, 2U) != HAL_OK)) {
      *status |= AGINTI_STATUS_CAPTURE_TIMEOUT;
      return false;
    }
    raw[i] = (uint16_t)HAL_ADC_GetValue(&hadc1);
    (void)HAL_ADC_Stop(&hadc1);
    direct_clock(config->sensor_clock_hz);
  }
  *status |= AGINTI_STATUS_ENGINE_DIRECT;
  return true;
}
#endif

#if AGINTI_CAPTURE_DMA
static bool timer_set_frequency(uint32_t sensor_clock_hz) {
  if (sensor_clock_hz == active_sensor_clock_hz) return true;
  const uint32_t period_ticks = timer_input_hz / sensor_clock_hz;
  if ((period_ticks < 16U) || (period_ticks > UINT16_MAX)) return false;
  active_sensor_clock_hz = timer_input_hz / period_ticks;
  TIM2->ARR = period_ticks - 1U;
  TIM2->CCR1 = period_ticks / 2U;
  TIM2->CCR2 = (period_ticks * 3U) / 4U;
  TIM2->EGR = TIM_EGR_UG;
  return true;
}

static bool dma_run_clock_block(uint16_t cycles) {
  if (cycles == 0U) return true;
  gpio_reset(C12880_CLK_PORT, C12880_CLK_PIN);
  if (HAL_DMA_Start(&hdma_clk_set, (uint32_t)&clk_set_word,
                    (uint32_t)&C12880_CLK_PORT->BSRR, cycles) != HAL_OK)
    return false;
  if (HAL_DMA_Start(&hdma_clk_reset, (uint32_t)&clk_reset_word,
                    (uint32_t)&C12880_CLK_PORT->BSRR, cycles) != HAL_OK) {
    (void)HAL_DMA_Abort(&hdma_clk_set);
    return false;
  }
  TIM2->CR1 &= ~TIM_CR1_CEN;
  TIM2->CNT = 0U;
  TIM2->SR = 0U;
  TIM2->DIER |= TIM_DIER_UDE | TIM_DIER_CC1DE;
  TIM2->EGR = TIM_EGR_UG;
  TIM2->CR1 |= TIM_CR1_CEN;
  const uint32_t expected =
      ((uint32_t)cycles * (SystemCoreClock / active_sensor_clock_hz));
  const uint32_t deadline = expected + (SystemCoreClock / 100U);
  const uint32_t start = DWT->CYCCNT;
  while (((DMA1_Stream2->CR & DMA_SxCR_EN) != 0U) &&
         ((uint32_t)(DWT->CYCCNT - start) < deadline)) {}
  TIM2->CR1 &= ~TIM_CR1_CEN;
  TIM2->DIER &= ~(TIM_DIER_UDE | TIM_DIER_CC1DE);
  gpio_reset(C12880_CLK_PORT, C12880_CLK_PIN);
  const bool complete = (DMA1_Stream2->CR & DMA_SxCR_EN) == 0U;
  (void)HAL_DMA_Abort(&hdma_clk_set);
  (void)HAL_DMA_Abort(&hdma_clk_reset);
  return complete;
}

static bool dma_run_clocks(uint32_t cycles) {
  while (cycles != 0U) {
    const uint16_t block =
        (cycles > UINT16_MAX) ? UINT16_MAX : (uint16_t)cycles;
    if (!dma_run_clock_block(block)) return false;
    cycles -= block;
  }
  return true;
}

static bool dma_capture(uint16_t *raw,
                        const aginti_c12880_config_t *config,
                        uint32_t *status) {
  if (!timer_set_frequency(config->sensor_clock_hz)) return false;
  if (!dma_run_clocks(C12880_PRE_CLOCKS)) return false;
  gpio_set(C12880_ST_PORT, C12880_ST_PIN);
  if (!dma_run_clocks(config->exposure_clocks)) return false;
  gpio_reset(C12880_ST_PORT, C12880_ST_PIN);
  if (!dma_run_clocks(C12880_DUMMY_CLOCKS_DMA)) return false;

  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)raw,
                        AGINTI_C12880_RAW_SAMPLES) != HAL_OK) return false;
  const bool clocks_ok = dma_run_clocks(AGINTI_C12880_RAW_SAMPLES);
  const uint32_t start = DWT->CYCCNT;
  while ((HAL_DMA_GetState(&hdma_adc1) != HAL_DMA_STATE_READY) &&
         ((uint32_t)(DWT->CYCCNT - start) < (SystemCoreClock / 100U))) {}
  const bool adc_ok = HAL_DMA_GetState(&hdma_adc1) == HAL_DMA_STATE_READY;
  (void)HAL_ADC_Stop_DMA(&hadc1);
  if (__HAL_ADC_GET_FLAG(&hadc1, ADC_FLAG_OVR) != 0U) {
    *status |= AGINTI_STATUS_ADC_OVERRUN;
    __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_OVR);
  }
  if (!clocks_ok || !adc_ok) {
    *status |= AGINTI_STATUS_CAPTURE_TIMEOUT;
    return false;
  }
  *status |= AGINTI_STATUS_ENGINE_DMA;
  return true;
}
#endif

aginti_capture_start_result_t board_capture(
    void *context, uint16_t *raw, const aginti_c12880_config_t *config,
    uint32_t *status) {
  (void)context;
  if ((raw == NULL) || (config == NULL) || (status == NULL))
    return AGINTI_CAPTURE_ERROR;
  set_output_mode(config->output_mode);
#if AGINTI_CAPTURE_DMA
  return dma_capture(raw, config, status) ? AGINTI_CAPTURE_COMPLETE
                                           : AGINTI_CAPTURE_ERROR;
#else
  return direct_capture(raw, config, status) ? AGINTI_CAPTURE_COMPLETE
                                              : AGINTI_CAPTURE_ERROR;
#endif
}

void board_capture_abort(void *context) {
  (void)context;
#if AGINTI_CAPTURE_DMA
  TIM2->CR1 &= ~TIM_CR1_CEN;
  TIM2->DIER &= ~(TIM_DIER_UDE | TIM_DIER_CC1DE);
  (void)HAL_ADC_Stop_DMA(&hadc1);
#else
  (void)HAL_ADC_Stop(&hadc1);
#endif
  gpio_reset(C12880_ST_PORT, C12880_ST_PIN);
  gpio_reset(C12880_CLK_PORT, C12880_CLK_PIN);
}

uint64_t board_time_us(void *context) {
  (void)context;
  return (uint64_t)HAL_GetTick() * 1000ULL +
         ((uint64_t)(SysTick->LOAD - SysTick->VAL) * 1000ULL /
          (uint64_t)(SysTick->LOAD + 1U));
}

uint32_t board_critical_enter(void *context) {
  (void)context;
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

void board_critical_exit(void *context, uint32_t token) {
  (void)context;
  if (token == 0U) __enable_irq();
}

void HAL_GPIO_EXTI_Callback(uint16_t pin) {
  if (pin == C12880_TRIGGER_PIN) external_trigger_pending = true;
}

bool board_external_trigger_take(void) {
  const uint32_t token = board_critical_enter(NULL);
  const bool pending = external_trigger_pending;
  external_trigger_pending = false;
  board_critical_exit(NULL, token);
  return pending;
}

static void i2c_delay(void) { dwt_delay(EEPROM_HALF_PERIOD_CYCLES); }
static void scl(bool high) {
  if (high) gpio_set(C12880_EEPROM_SCL_PORT, C12880_EEPROM_SCL_PIN);
  else gpio_reset(C12880_EEPROM_SCL_PORT, C12880_EEPROM_SCL_PIN);
  i2c_delay();
}
static void sda(bool high) {
  if (high) gpio_set(C12880_EEPROM_SDA_PORT, C12880_EEPROM_SDA_PIN);
  else gpio_reset(C12880_EEPROM_SDA_PORT, C12880_EEPROM_SDA_PIN);
  i2c_delay();
}
static bool sda_read(void) {
  return (C12880_EEPROM_SDA_PORT->IDR & C12880_EEPROM_SDA_PIN) != 0U;
}
static void i2c_start(void) { sda(true); scl(true); sda(false); scl(false); }
static void i2c_stop(void) { sda(false); scl(true); sda(true); }
static bool i2c_write_byte(uint8_t value) {
  for (uint32_t bit = 0; bit < 8U; ++bit) {
    sda((value & 0x80U) != 0U); scl(true); scl(false); value <<= 1;
  }
  sda(true); scl(true); const bool ack = !sda_read(); scl(false); return ack;
}
static uint8_t i2c_read_byte(bool acknowledge) {
  uint8_t value = 0U; sda(true);
  for (uint32_t bit = 0; bit < 8U; ++bit) {
    value <<= 1; scl(true); if (sda_read()) value |= 1U; scl(false);
  }
  sda(!acknowledge); scl(true); scl(false); sda(true); return value;
}

bool board_eeprom_read(uint16_t address, uint8_t *data, size_t bytes) {
  if ((data == NULL) || (bytes == 0U)) return false;
  i2c_start();
  if (!i2c_write_byte(EEPROM_DEVICE_WRITE_ADDRESS) ||
      !i2c_write_byte((uint8_t)(address >> 8)) ||
      !i2c_write_byte((uint8_t)address)) { i2c_stop(); return false; }
  i2c_start();
  if (!i2c_write_byte(EEPROM_DEVICE_READ_ADDRESS)) { i2c_stop(); return false; }
  for (size_t i = 0; i < bytes; ++i)
    data[i] = i2c_read_byte((i + 1U) < bytes);
  i2c_stop();
  return true;
}

void board_safe_outputs(void) {
  gpio_reset(C12880_ST_PORT, C12880_ST_PIN);
  gpio_reset(C12880_CLK_PORT, C12880_CLK_PIN);
  gpio_reset(C12880_MODE_PORT, C12880_MODE0_PIN | C12880_MODE1_PIN);
}

void board_panic(uint32_t code) {
  (void)code;
  __disable_irq();
  board_safe_outputs();
  for (;;) __WFI();
}

void assert_failed(uint8_t *file, uint32_t line) {
  (void)file; board_panic(line);
}
