#ifndef AGINTI_C12880_H
#define AGINTI_C12880_H

#include "aginti/c12880_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__)
#define AGINTI_ALIGN32 __attribute__((aligned(32)))
#else
#define AGINTI_ALIGN32
#endif

enum {
  AGINTI_STATUS_OK = 0U,
  AGINTI_STATUS_CAPTURE_TIMEOUT = 1U << 0,
  AGINTI_STATUS_ADC_OVERRUN = 1U << 1,
  AGINTI_STATUS_USB_BACKPRESSURE = 1U << 2,
  AGINTI_STATUS_CONFIG_CLAMPED = 1U << 3,
  AGINTI_STATUS_EXTERNAL_TRIGGER = 1U << 4,
  AGINTI_STATUS_ENGINE_DIRECT = 1U << 5,
  AGINTI_STATUS_ENGINE_DMA = 1U << 6
};

typedef struct {
  uint32_t sensor_clock_hz;
  uint32_t exposure_clocks;
  uint8_t output_mode;
  aginti_frame_format_t frame_format;
} aginti_c12880_config_t;

typedef struct {
  uint32_t sequence;
  uint64_t timestamp_us;
  uint32_t status;
  aginti_c12880_config_t config;
} aginti_c12880_metadata_t;

typedef enum {
  AGINTI_SLOT_FREE = 0,
  AGINTI_SLOT_CAPTURING,
  AGINTI_SLOT_READY,
  AGINTI_SLOT_TRANSMITTING
} aginti_slot_state_t;

typedef struct AGINTI_ALIGN32 {
  volatile aginti_slot_state_t state;
  uint16_t tx_bytes;
  uint16_t reserved;
  aginti_c12880_metadata_t metadata;
  uint16_t raw[AGINTI_C12880_RAW_SAMPLES];
  uint8_t tx[AGINTI_C12880_MAX_FRAME_BYTES];
} aginti_c12880_slot_t;

typedef enum {
  AGINTI_CAPTURE_ERROR = -1,
  AGINTI_CAPTURE_COMPLETE = 0,
  AGINTI_CAPTURE_ASYNC = 1
} aginti_capture_start_result_t;

typedef struct {
  aginti_capture_start_result_t (*capture_start)(
      void *context, uint16_t *raw, const aginti_c12880_config_t *config,
      uint32_t *status);
  void (*capture_abort)(void *context);
  int (*transport_submit)(void *context, const uint8_t *data, size_t bytes);
  bool (*transport_busy)(void *context);
  uint64_t (*time_us)(void *context);
  uint32_t (*critical_enter)(void *context);
  void (*critical_exit)(void *context, uint32_t token);
  void *context;
} aginti_c12880_platform_t;

typedef struct {
  aginti_c12880_platform_t platform;
  aginti_c12880_config_t config;
  aginti_c12880_slot_t slots[2];
  volatile uint8_t pending_single;
  volatile bool streaming;
  volatile bool capture_active;
  uint32_t next_sequence;
  uint32_t frames_captured;
  uint32_t frames_transmitted;
  uint32_t dropped_frames;
  uint32_t capture_errors;
  uint32_t transport_errors;
} aginti_c12880_device_t;

void aginti_c12880_init(aginti_c12880_device_t *device,
                        const aginti_c12880_platform_t *platform);
bool aginti_c12880_configure(aginti_c12880_device_t *device,
                             const aginti_c12880_config_t *config);
void aginti_c12880_request_single(aginti_c12880_device_t *device);
void aginti_c12880_start_stream(aginti_c12880_device_t *device);
void aginti_c12880_stop_stream(aginti_c12880_device_t *device);
void aginti_c12880_poll(aginti_c12880_device_t *device);
void aginti_c12880_capture_complete(aginti_c12880_device_t *device,
                                    uint32_t status);
void aginti_c12880_transport_complete(aginti_c12880_device_t *device);

#ifdef __cplusplus
}
#endif

#endif

