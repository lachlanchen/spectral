#ifndef AGINTI_COORDINATOR_BOARD_H
#define AGINTI_COORDINATOR_BOARD_H

#include "aginti_coordinator.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COORD_LAMP1_PORT GPIOA
#define COORD_LAMP1_PIN GPIO_PIN_0
#define COORD_LAMP2_PORT GPIOA
#define COORD_LAMP2_PIN GPIO_PIN_1
#define COORD_I2C_PORT GPIOB
#define COORD_I2C_SCL_PIN GPIO_PIN_8
#define COORD_I2C_SDA_PIN GPIO_PIN_9
#define COORD_TRIGGER_PORT GPIOG
#define COORD_TRIGGER_PIN GPIO_PIN_4

enum {
  COORD_SYNC_MAGIC = 0x5347U,
  COORD_SYNC_VERSION = 1U
};

typedef struct __attribute__((packed)) {
  uint16_t magic;
  uint8_t version;
  uint8_t flags;
  uint32_t sequence;
  uint32_t timestamp_us;
  uint16_t lut_index;
  uint16_t duty1_q16;
  uint16_t duty2_q16;
  uint16_t crc16;
} aginti_coord_sync_packet_t;

bool coord_board_init(void);
uint32_t coord_board_now_us(void);
uint32_t coord_board_pwm_period_us(void);
bool coord_board_pwm_stage(uint16_t duty1_q16, uint16_t duty2_q16);
bool coord_board_pwm_take_commit(uint32_t *timestamp_us);
void coord_board_force_off(void);
void coord_board_trigger_pulse(uint32_t width_us);
void coord_board_send_sync(uint32_t sequence, uint32_t timestamp_us,
                           uint16_t lut_index, uint16_t duty1_q16,
                           uint16_t duty2_q16);
bool coord_board_read_telemetry(aginti_coord_telemetry_t *telemetry);
uint32_t coord_board_probe_flags(void);
bool coord_board_host_read_byte(uint8_t *value);
void coord_board_host_write(const char *text);
void coord_board_panic(uint32_t code) __attribute__((noreturn));

#endif
