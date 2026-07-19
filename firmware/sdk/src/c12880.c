#include "aginti/c12880.h"

#include <string.h>

#define AGINTI_DEFAULT_SENSOR_CLOCK_HZ 1000000U
#define AGINTI_MIN_SENSOR_CLOCK_HZ 100000U
#define AGINTI_MAX_SENSOR_CLOCK_HZ 5000000U
#define AGINTI_MIN_EXPOSURE_US 3U
#define AGINTI_MAX_EXPOSURE_CLOCKS 2000000U

static uint32_t enter_critical(aginti_c12880_device_t *device) {
  return (device->platform.critical_enter != NULL)
             ? device->platform.critical_enter(device->platform.context)
             : 0U;
}

static void exit_critical(aginti_c12880_device_t *device, uint32_t token) {
  if (device->platform.critical_exit != NULL) {
    device->platform.critical_exit(device->platform.context, token);
  }
}

static aginti_c12880_slot_t *find_slot(aginti_c12880_device_t *device,
                                      aginti_slot_state_t state) {
  for (size_t i = 0; i < 2U; ++i) {
    if (device->slots[i].state == state) return &device->slots[i];
  }
  return NULL;
}

static uint16_t pack_legacy(aginti_c12880_slot_t *slot) {
  memset(slot->tx, 0, AGINTI_C12880_LEGACY_HEADER_BYTES);
  memcpy(&slot->tx[AGINTI_C12880_LEGACY_HEADER_BYTES], slot->raw,
         AGINTI_C12880_LEGACY_FRAME_BYTES -
             AGINTI_C12880_LEGACY_HEADER_BYTES);
  return AGINTI_C12880_LEGACY_FRAME_BYTES;
}

static uint16_t pack_v2(aginti_c12880_device_t *device,
                        aginti_c12880_slot_t *slot) {
  aginti_v2_frame_header_t header;
  memset(&header, 0, sizeof(header));
  memcpy(header.magic, "ASP2", 4U);
  header.version = 2U;
  header.header_bytes = AGINTI_C12880_V2_HEADER_BYTES;
  header.flags = 1U;
  header.sequence = slot->metadata.sequence;
  header.payload_bytes = AGINTI_C12880_RAW_SAMPLES * 2U;
  header.timestamp_us = slot->metadata.timestamp_us;
  header.exposure_clocks = slot->metadata.config.exposure_clocks;
  header.sensor_clock_hz = slot->metadata.config.sensor_clock_hz;
  header.active_pixels = AGINTI_C12880_ACTIVE_PIXELS;
  header.raw_samples = AGINTI_C12880_RAW_SAMPLES;
  header.status = slot->metadata.status;
  header.dropped_frames = device->dropped_frames;
  memcpy(slot->tx, &header, sizeof(header));
  memcpy(&slot->tx[sizeof(header)], slot->raw, sizeof(slot->raw));
  const uint32_t crc = aginti_crc32(slot->tx,
                                    AGINTI_C12880_V2_FRAME_BYTES, 0U);
  memcpy(&slot->tx[offsetof(aginti_v2_frame_header_t, crc32)], &crc,
         sizeof(crc));
  return AGINTI_C12880_V2_FRAME_BYTES;
}

void aginti_c12880_init(aginti_c12880_device_t *device,
                        const aginti_c12880_platform_t *platform) {
  if ((device == NULL) || (platform == NULL)) return;
  memset(device, 0, sizeof(*device));
  device->platform = *platform;
  device->config.sensor_clock_hz = AGINTI_DEFAULT_SENSOR_CLOCK_HZ;
  device->config.exposure_clocks = 1000U;
  device->config.frame_format = AGINTI_FRAME_V2;
}

bool aginti_c12880_configure(aginti_c12880_device_t *device,
                             const aginti_c12880_config_t *config) {
  if ((device == NULL) || (config == NULL)) return false;
  aginti_c12880_config_t value = *config;
  bool exact = true;
  if (value.sensor_clock_hz < AGINTI_MIN_SENSOR_CLOCK_HZ) {
    value.sensor_clock_hz = AGINTI_MIN_SENSOR_CLOCK_HZ;
    exact = false;
  }
  if (value.sensor_clock_hz > AGINTI_MAX_SENSOR_CLOCK_HZ) {
    value.sensor_clock_hz = AGINTI_MAX_SENSOR_CLOCK_HZ;
    exact = false;
  }
  const uint32_t minimum_exposure_clocks =
      ((value.sensor_clock_hz * AGINTI_MIN_EXPOSURE_US) + 999999U) /
      1000000U;
  if (value.exposure_clocks < minimum_exposure_clocks) {
    value.exposure_clocks = minimum_exposure_clocks;
    exact = false;
  }
  if (value.exposure_clocks > AGINTI_MAX_EXPOSURE_CLOCKS) {
    value.exposure_clocks = AGINTI_MAX_EXPOSURE_CLOCKS;
    exact = false;
  }
  if (value.frame_format > AGINTI_FRAME_V2) {
    value.frame_format = AGINTI_FRAME_V2;
    exact = false;
  }
  const uint32_t token = enter_critical(device);
  device->config = value;
  exit_critical(device, token);
  return exact;
}

void aginti_c12880_request_single(aginti_c12880_device_t *device) {
  if (device == NULL) return;
  const uint32_t token = enter_critical(device);
  if (device->pending_single != UINT8_MAX) ++device->pending_single;
  else ++device->dropped_frames;
  exit_critical(device, token);
}

void aginti_c12880_start_stream(aginti_c12880_device_t *device) {
  if (device != NULL) device->streaming = true;
}

void aginti_c12880_stop_stream(aginti_c12880_device_t *device) {
  if (device == NULL) return;
  device->streaming = false;
  if (device->platform.capture_abort != NULL) {
    device->platform.capture_abort(device->platform.context);
  }
}

void aginti_c12880_capture_complete(aginti_c12880_device_t *device,
                                    uint32_t status) {
  if (device == NULL) return;
  const uint32_t token = enter_critical(device);
  aginti_c12880_slot_t *slot = find_slot(device, AGINTI_SLOT_CAPTURING);
  if (slot != NULL) {
    slot->metadata.status |= status;
    slot->metadata.timestamp_us =
        (device->platform.time_us != NULL)
            ? device->platform.time_us(device->platform.context)
            : 0U;
    slot->state = AGINTI_SLOT_READY;
    ++device->frames_captured;
  }
  device->capture_active = false;
  exit_critical(device, token);
}

void aginti_c12880_transport_complete(aginti_c12880_device_t *device) {
  if (device == NULL) return;
  const uint32_t token = enter_critical(device);
  aginti_c12880_slot_t *slot = find_slot(device, AGINTI_SLOT_TRANSMITTING);
  if (slot != NULL) {
    slot->state = AGINTI_SLOT_FREE;
    ++device->frames_transmitted;
  }
  exit_critical(device, token);
}

static void submit_ready(aginti_c12880_device_t *device) {
  if ((device->platform.transport_submit == NULL) ||
      ((device->platform.transport_busy != NULL) &&
       device->platform.transport_busy(device->platform.context))) return;
  aginti_c12880_slot_t *slot = find_slot(device, AGINTI_SLOT_READY);
  if (slot == NULL) return;
  slot->tx_bytes = (slot->metadata.config.frame_format == AGINTI_FRAME_LEGACY)
                       ? pack_legacy(slot)
                       : pack_v2(device, slot);
  const int result = device->platform.transport_submit(
      device->platform.context, slot->tx, slot->tx_bytes);
  if (result == 0) slot->state = AGINTI_SLOT_TRANSMITTING;
  else if (result < 0) {
    ++device->transport_errors;
    slot->state = AGINTI_SLOT_FREE;
  }
}

static void start_capture_if_possible(aginti_c12880_device_t *device) {
  if (device->capture_active ||
      (!device->streaming && (device->pending_single == 0U)) ||
      (device->platform.capture_start == NULL)) return;
  aginti_c12880_slot_t *slot = find_slot(device, AGINTI_SLOT_FREE);
  if (slot == NULL) return;
  const uint32_t token = enter_critical(device);
  slot->state = AGINTI_SLOT_CAPTURING;
  slot->metadata.sequence = device->next_sequence++;
  slot->metadata.status = 0U;
  slot->metadata.config = device->config;
  device->capture_active = true;
  if (!device->streaming && (device->pending_single != 0U)) {
    --device->pending_single;
  }
  exit_critical(device, token);
  uint32_t status = 0U;
  const aginti_capture_start_result_t result = device->platform.capture_start(
      device->platform.context, slot->raw, &slot->metadata.config, &status);
  if (result == AGINTI_CAPTURE_COMPLETE) {
    aginti_c12880_capture_complete(device, status);
  } else if (result == AGINTI_CAPTURE_ERROR) {
    const uint32_t failure_token = enter_critical(device);
    slot->state = AGINTI_SLOT_FREE;
    device->capture_active = false;
    ++device->capture_errors;
    exit_critical(device, failure_token);
  }
}

void aginti_c12880_poll(aginti_c12880_device_t *device) {
  if (device == NULL) return;
  submit_ready(device);
  start_capture_if_possible(device);
  submit_ready(device);
}
