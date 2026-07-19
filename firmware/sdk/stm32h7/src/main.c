#include "aginti/c12880.h"
#include "aginti/c12880_protocol.h"
#include "c12880_board.h"
#include "c12880_usb.h"

#include <string.h>

typedef enum { TX_OWNER_NONE = 0, TX_OWNER_FRAME, TX_OWNER_CONTROL } tx_owner_t;

typedef struct __attribute__((packed)) {
  uint32_t firmware_version;
  uint32_t sensor_clock_min_hz;
  uint32_t sensor_clock_default_hz;
  uint32_t sensor_clock_max_hz;
  uint32_t exposure_min_clocks;
  uint32_t exposure_max_clocks;
  uint16_t active_pixels;
  uint16_t raw_samples;
  uint16_t legacy_frame_bytes;
  uint16_t v2_frame_bytes;
  uint32_t feature_flags;
} capabilities_t;

typedef struct __attribute__((packed)) {
  uint32_t frames_captured;
  uint32_t frames_transmitted;
  uint32_t dropped_frames;
  uint32_t capture_errors;
  uint32_t transport_errors;
  uint32_t parser_errors;
  uint32_t usb_rx_overruns;
  uint32_t streaming;
} status_reply_t;

__attribute__((section(".dma_buffer"), aligned(32)))
static aginti_c12880_device_t device;
static aginti_protocol_parser_t parser;
__attribute__((section(".dma_buffer"), aligned(32)))
static uint8_t control_tx[4096U + 32U];
static volatile uint16_t control_tx_bytes;
static volatile tx_owner_t tx_owner;

static int transport_submit(void *context, const uint8_t *data, size_t bytes) {
  (void)context;
  if (c12880_usb_busy() || (tx_owner != TX_OWNER_NONE)) return 1;
  tx_owner = TX_OWNER_FRAME;
  const int result = c12880_usb_transmit(data, bytes);
  if (result != 0) tx_owner = TX_OWNER_NONE;
  return result;
}

static bool transport_busy(void *context) {
  (void)context;
  return c12880_usb_busy() || (tx_owner != TX_OWNER_NONE) ||
         (control_tx_bytes != 0U);
}

void app_usb_tx_complete_isr(void) {
  const tx_owner_t owner = tx_owner;
  tx_owner = TX_OWNER_NONE;
  if (owner == TX_OWNER_FRAME) aginti_c12880_transport_complete(&device);
}

static bool queue_control(const void *data, size_t bytes) {
  if ((data == NULL) || (bytes > sizeof(control_tx)) ||
      (control_tx_bytes != 0U)) return false;
  memcpy(control_tx, data, bytes);
  control_tx_bytes = (uint16_t)bytes;
  return true;
}

static void service_control_tx(void) {
  if ((control_tx_bytes == 0U) || c12880_usb_busy() ||
      (tx_owner != TX_OWNER_NONE)) return;
  tx_owner = TX_OWNER_CONTROL;
  const uint16_t bytes = control_tx_bytes;
  if (c12880_usb_transmit(control_tx, bytes) == 0) control_tx_bytes = 0U;
  else tx_owner = TX_OWNER_NONE;
}

static uint32_t read_le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void reply_v2(const aginti_command_t *command, uint16_t status,
                     const void *payload, uint16_t payload_bytes) {
  if (control_tx_bytes != 0U) return;
  control_tx_bytes = (uint16_t)aginti_v2_build_reply(
      control_tx, sizeof(control_tx), command->opcode, command->sequence,
      status, payload, payload_bytes);
}

static void handle_command(const aginti_command_t *command) {
  static const uint8_t identity[8] = {0x04U, 0x06U, 'c', '1', '2', '8', '8', '0'};
  switch (command->kind) {
    case AGINTI_CMD_IDENTITY:
      (void)queue_control(identity, sizeof(identity));
      break;
    case AGINTI_CMD_LEGACY_ACQUIRE: {
      aginti_c12880_config_t config = device.config;
      config.frame_format = AGINTI_FRAME_LEGACY;
      config.output_mode = command->output_mode;
      if (command->exposure_clocks != 0U)
        config.exposure_clocks = command->exposure_clocks;
      (void)aginti_c12880_configure(&device, &config);
      aginti_c12880_request_single(&device);
      break;
    }
    case AGINTI_CMD_LEGACY_EEPROM_READ:
      aginti_c12880_stop_stream(&device);
      if (board_eeprom_read(0U, control_tx, 4096U)) control_tx_bytes = 4096U;
      break;
    case AGINTI_CMD_LEGACY_CAL_READ:
      aginti_c12880_stop_stream(&device);
      if (board_eeprom_read(0x1068U, control_tx, 0x30U)) control_tx_bytes = 0x30U;
      break;
    case AGINTI_CMD_LEGACY_EEPROM_WRITE:
    case AGINTI_CMD_LEGACY_CAL_WRITE:
      /* Deliberately read-only: calibration writes require an explicit future unlock. */
      break;
    case AGINTI_CMD_V2_HELLO: {
      static const char name[] = "AgInTi C12880MA clean-room firmware 0.2";
      reply_v2(command, 0U, name, (uint16_t)sizeof(name));
      break;
    }
    case AGINTI_CMD_V2_GET_CAPS: {
      const capabilities_t caps = {
          0x00020000U, 100000U, 1000000U, 2000000U, 11U, 2000000U,
          AGINTI_C12880_ACTIVE_PIXELS, AGINTI_C12880_RAW_SAMPLES,
          AGINTI_C12880_LEGACY_FRAME_BYTES, AGINTI_C12880_V2_FRAME_BYTES,
          0x0000001FU};
      reply_v2(command, 0U, &caps, sizeof(caps));
      break;
    }
    case AGINTI_CMD_V2_CONFIGURE: {
      uint16_t result = 1U;
      if (command->payload_bytes >= 12U) {
        aginti_c12880_config_t config = device.config;
        config.sensor_clock_hz = read_le32(&command->payload[0]);
        config.exposure_clocks = read_le32(&command->payload[4]);
        config.output_mode = command->payload[8];
        config.frame_format = (aginti_frame_format_t)command->payload[9];
        result = aginti_c12880_configure(&device, &config) ? 0U : 2U;
      }
      reply_v2(command, result, &device.config, sizeof(device.config));
      break;
    }
    case AGINTI_CMD_V2_SINGLE_SHOT:
      aginti_c12880_request_single(&device);
      reply_v2(command, 0U, NULL, 0U);
      break;
    case AGINTI_CMD_V2_START_STREAM:
      aginti_c12880_start_stream(&device);
      reply_v2(command, 0U, NULL, 0U);
      break;
    case AGINTI_CMD_V2_STOP_STREAM:
      aginti_c12880_stop_stream(&device);
      reply_v2(command, 0U, NULL, 0U);
      break;
    case AGINTI_CMD_V2_GET_STATUS: {
      const status_reply_t status = {
          device.frames_captured, device.frames_transmitted,
          device.dropped_frames, device.capture_errors,
          device.transport_errors,
          parser.malformed_packets + parser.crc_failures + parser.queue_overruns,
          c12880_usb_rx_overruns(), device.streaming ? 1U : 0U};
      reply_v2(command, 0U, &status, sizeof(status));
      break;
    }
    case AGINTI_CMD_V2_EEPROM_READ: {
      uint16_t result = 1U;
      if (command->payload_bytes >= 4U) {
        const uint16_t address = (uint16_t)(command->payload[0] |
                                            ((uint16_t)command->payload[1] << 8));
        uint16_t bytes = (uint16_t)(command->payload[2] |
                                    ((uint16_t)command->payload[3] << 8));
        if (bytes > AGINTI_C12880_COMMAND_PAYLOAD_MAX) bytes = AGINTI_C12880_COMMAND_PAYLOAD_MAX;
        uint8_t payload[AGINTI_C12880_COMMAND_PAYLOAD_MAX];
        if (board_eeprom_read(address, payload, bytes)) {
          reply_v2(command, 0U, payload, bytes);
          result = 0U;
        }
      }
      if (result != 0U) reply_v2(command, result, NULL, 0U);
      break;
    }
    default: break;
  }
}

int main(void) {
  board_mpu_config();
  SCB_EnableICache();
  SCB_EnableDCache();
  HAL_Init();
  if (!board_clock_config() || !board_peripherals_init()) board_panic(1U);

  const aginti_c12880_platform_t platform = {
      board_capture, board_capture_abort, transport_submit, transport_busy,
      board_time_us, board_critical_enter, board_critical_exit, NULL};
  aginti_c12880_init(&device, &platform);
  aginti_protocol_parser_init(&parser);
  if (!c12880_usb_init()) board_panic(2U);

  uint8_t rx[128];
  for (;;) {
    const size_t received = c12880_usb_read(rx, sizeof(rx));
    if (received != 0U) aginti_protocol_parser_feed(&parser, rx, received);
    aginti_command_t command;
    while (aginti_protocol_parser_pop(&parser, &command)) handle_command(&command);
    if (board_external_trigger_take()) {
      aginti_c12880_request_single(&device);
    }
    service_control_tx();
    aginti_c12880_poll(&device);
  }
}

