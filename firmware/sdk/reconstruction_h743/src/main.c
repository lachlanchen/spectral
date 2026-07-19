#include "aginti/c12880.h"
#include "aginti/c12880_protocol.h"
#include "c12880_board.h"
#include "c12880_usb.h"

#include <string.h>

enum {
  VENDOR_SENSOR_CLOCK_HZ = 5000000U,
  VENDOR_DEFAULT_EXPOSURE_CLOCKS = 50U,
  VENDOR_CORRECTION_BYTES = 1024U
};

typedef enum { TX_NONE = 0, TX_FRAME, TX_CONTROL } tx_owner_t;

__attribute__((section(".dma_buffer"), aligned(32)))
static aginti_c12880_device_t device;
static aginti_protocol_parser_t parser;
__attribute__((section(".dma_buffer"), aligned(32)))
static uint8_t control_tx[VENDOR_CORRECTION_BYTES];
static volatile uint16_t control_tx_bytes;
static volatile tx_owner_t tx_owner;

static int transport_submit(void *context, const uint8_t *data, size_t bytes) {
  (void)context;
  if (c12880_usb_busy() || (tx_owner != TX_NONE)) return 1;
  tx_owner = TX_FRAME;
  const int result = c12880_usb_transmit(data, bytes);
  if (result != 0) tx_owner = TX_NONE;
  return result;
}

static bool transport_busy(void *context) {
  (void)context;
  return c12880_usb_busy() || (tx_owner != TX_NONE) ||
         (control_tx_bytes != 0U);
}

void app_usb_tx_complete_isr(void) {
  const tx_owner_t owner = tx_owner;
  tx_owner = TX_NONE;
  if (owner == TX_FRAME) aginti_c12880_transport_complete(&device);
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
      (tx_owner != TX_NONE)) return;
  tx_owner = TX_CONTROL;
  const uint16_t bytes = control_tx_bytes;
  if (c12880_usb_transmit(control_tx, bytes) == 0) control_tx_bytes = 0U;
  else tx_owner = TX_NONE;
}

static void configure_legacy(const aginti_command_t *command) {
  aginti_c12880_config_t config = device.config;
  config.sensor_clock_hz = VENDOR_SENSOR_CLOCK_HZ;
  config.frame_format = AGINTI_FRAME_LEGACY;
  config.output_mode = command->output_mode;
  if (command->exposure_clocks != 0U)
    config.exposure_clocks = command->exposure_clocks;
  (void)aginti_c12880_configure(&device, &config);
}

static void handle_command(const aginti_command_t *command) {
  static const uint8_t identity[8] = {
      0x04U, 0x06U, 'c', '1', '2', '8', '8', '0'};

  switch (command->kind) {
    case AGINTI_CMD_IDENTITY:
      (void)queue_control(identity, sizeof(identity));
      break;
    case AGINTI_CMD_LEGACY_CONFIGURE:
      configure_legacy(command);
      break;
    case AGINTI_CMD_LEGACY_ACQUIRE:
      configure_legacy(command);
      aginti_c12880_request_single(&device);
      break;
    case AGINTI_CMD_LEGACY_EEPROM_READ:
      aginti_c12880_stop_stream(&device);
      if (board_eeprom_read(0U, control_tx, VENDOR_CORRECTION_BYTES))
        control_tx_bytes = VENDOR_CORRECTION_BYTES;
      break;
    case AGINTI_CMD_LEGACY_CAL_READ:
      aginti_c12880_stop_stream(&device);
      if (board_eeprom_read(0x1068U, control_tx, 0x30U))
        control_tx_bytes = 0x30U;
      break;
    case AGINTI_CMD_LEGACY_EEPROM_WRITE:
    case AGINTI_CMD_LEGACY_CAL_WRITE:
      /* Clean-room safety boundary: calibration storage is read-only. */
      break;
    default:
      /* V2 and unknown commands are intentionally absent in this profile. */
      break;
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
  const aginti_c12880_config_t defaults = {
      VENDOR_SENSOR_CLOCK_HZ, VENDOR_DEFAULT_EXPOSURE_CLOCKS, 1U,
      AGINTI_FRAME_LEGACY};
  (void)aginti_c12880_configure(&device, &defaults);
  aginti_protocol_parser_init(&parser);
  if (!c12880_usb_init()) board_panic(2U);

  uint8_t rx[128];
  for (;;) {
    const size_t received = c12880_usb_read(rx, sizeof(rx));
    if (received != 0U) aginti_protocol_parser_feed(&parser, rx, received);
    aginti_command_t command;
    while (aginti_protocol_parser_pop(&parser, &command))
      handle_command(&command);
    if (board_external_trigger_take())
      aginti_c12880_request_single(&device);
    service_control_tx();
    aginti_c12880_poll(&device);
  }
}
