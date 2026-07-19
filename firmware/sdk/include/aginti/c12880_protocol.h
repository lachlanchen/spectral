#ifndef AGINTI_C12880_PROTOCOL_H
#define AGINTI_C12880_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AGINTI_C12880_ACTIVE_PIXELS 288U
#define AGINTI_C12880_RAW_SAMPLES 294U
#define AGINTI_C12880_LEGACY_HEADER_BYTES 12U
#define AGINTI_C12880_LEGACY_FRAME_BYTES 590U
#define AGINTI_C12880_V2_HEADER_BYTES 48U
#define AGINTI_C12880_V2_FRAME_BYTES \
  (AGINTI_C12880_V2_HEADER_BYTES + (AGINTI_C12880_RAW_SAMPLES * 2U))
#define AGINTI_C12880_MAX_FRAME_BYTES AGINTI_C12880_V2_FRAME_BYTES
#define AGINTI_C12880_COMMAND_PAYLOAD_MAX 64U

typedef enum {
  AGINTI_FRAME_LEGACY = 0,
  AGINTI_FRAME_V2 = 1
} aginti_frame_format_t;

typedef enum {
  AGINTI_CMD_NONE = 0,
  AGINTI_CMD_IDENTITY,
  AGINTI_CMD_LEGACY_ACQUIRE,
  AGINTI_CMD_LEGACY_EEPROM_WRITE,
  AGINTI_CMD_LEGACY_EEPROM_READ,
  AGINTI_CMD_LEGACY_CAL_READ,
  AGINTI_CMD_LEGACY_CAL_WRITE,
  AGINTI_CMD_V2_HELLO = 0x81,
  AGINTI_CMD_V2_GET_CAPS = 0x82,
  AGINTI_CMD_V2_CONFIGURE = 0x90,
  AGINTI_CMD_V2_SINGLE_SHOT = 0x91,
  AGINTI_CMD_V2_START_STREAM = 0x92,
  AGINTI_CMD_V2_STOP_STREAM = 0x93,
  AGINTI_CMD_V2_GET_STATUS = 0x94,
  AGINTI_CMD_V2_EEPROM_READ = 0xA0
} aginti_command_kind_t;

enum {
  AGINTI_V2_OPCODE_HELLO = 0x01,
  AGINTI_V2_OPCODE_GET_CAPS = 0x02,
  AGINTI_V2_OPCODE_CONFIGURE = 0x10,
  AGINTI_V2_OPCODE_SINGLE_SHOT = 0x11,
  AGINTI_V2_OPCODE_START_STREAM = 0x12,
  AGINTI_V2_OPCODE_STOP_STREAM = 0x13,
  AGINTI_V2_OPCODE_GET_STATUS = 0x14,
  AGINTI_V2_OPCODE_EEPROM_READ = 0x20
};

typedef struct {
  aginti_command_kind_t kind;
  uint8_t opcode;
  uint8_t output_mode;
  uint16_t payload_bytes;
  uint32_t sequence;
  uint32_t exposure_clocks;
  uint8_t payload[AGINTI_C12880_COMMAND_PAYLOAD_MAX];
} aginti_command_t;

typedef struct {
  uint8_t input[160];
  uint16_t input_bytes;
  aginti_command_t queue[4];
  uint8_t queue_head;
  uint8_t queue_tail;
  uint32_t malformed_packets;
  uint32_t crc_failures;
  uint32_t queue_overruns;
} aginti_protocol_parser_t;

#if defined(__GNUC__)
#define AGINTI_PACKED __attribute__((packed))
#else
#define AGINTI_PACKED
#pragma pack(push, 1)
#endif

typedef struct AGINTI_PACKED {
  uint8_t magic[4];
  uint8_t version;
  uint8_t header_bytes;
  uint16_t flags;
  uint32_t sequence;
  uint32_t payload_bytes;
  uint64_t timestamp_us;
  uint32_t exposure_clocks;
  uint32_t sensor_clock_hz;
  uint16_t active_pixels;
  uint16_t raw_samples;
  uint32_t status;
  uint32_t dropped_frames;
  uint32_t crc32;
} aginti_v2_frame_header_t;

typedef struct AGINTI_PACKED {
  uint8_t magic[4];
  uint8_t version;
  uint8_t opcode;
  uint16_t flags;
  uint32_t sequence;
  uint16_t payload_bytes;
  uint16_t reserved;
  uint32_t crc32;
} aginti_v2_command_header_t;

#if !defined(__GNUC__)
#pragma pack(pop)
#endif

void aginti_protocol_parser_init(aginti_protocol_parser_t *parser);
void aginti_protocol_parser_feed(aginti_protocol_parser_t *parser,
                                 const uint8_t *data, size_t bytes);
bool aginti_protocol_parser_pop(aginti_protocol_parser_t *parser,
                                aginti_command_t *command);
uint32_t aginti_crc32(const uint8_t *data, size_t bytes, uint32_t seed);
size_t aginti_v2_build_reply(uint8_t *destination, size_t capacity,
                             uint8_t opcode, uint32_t sequence,
                             uint16_t status, const void *payload,
                             uint16_t payload_bytes);

#ifdef __cplusplus
}
#endif

#endif

