#include "aginti/c12880_protocol.h"

#include <string.h>

_Static_assert(sizeof(aginti_v2_frame_header_t) == AGINTI_C12880_V2_HEADER_BYTES,
               "v2 frame header layout changed");
_Static_assert(sizeof(aginti_v2_command_header_t) == 20U,
               "v2 command header layout changed");

static uint16_t read_le16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_le16(uint8_t *p, uint16_t value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *p, uint32_t value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
  p[2] = (uint8_t)(value >> 16);
  p[3] = (uint8_t)(value >> 24);
}

uint32_t aginti_crc32(const uint8_t *data, size_t bytes, uint32_t seed) {
  uint32_t crc = ~seed;
  for (size_t i = 0; i < bytes; ++i) {
    crc ^= data[i];
    for (uint32_t bit = 0; bit < 8U; ++bit) {
      const uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1) ^ (0xEDB88320U & mask);
    }
  }
  return ~crc;
}

void aginti_protocol_parser_init(aginti_protocol_parser_t *parser) {
  if (parser != NULL) {
    memset(parser, 0, sizeof(*parser));
  }
}

static bool queue_push(aginti_protocol_parser_t *parser,
                       const aginti_command_t *command) {
  const uint8_t next = (uint8_t)((parser->queue_head + 1U) & 3U);
  if (next == parser->queue_tail) {
    ++parser->queue_overruns;
    return false;
  }
  parser->queue[parser->queue_head] = *command;
  parser->queue_head = next;
  return true;
}

static void discard_prefix(aginti_protocol_parser_t *parser, uint16_t bytes) {
  if (bytes >= parser->input_bytes) {
    parser->input_bytes = 0U;
    return;
  }
  memmove(parser->input, &parser->input[bytes],
          (size_t)(parser->input_bytes - bytes));
  parser->input_bytes = (uint16_t)(parser->input_bytes - bytes);
}

static aginti_command_kind_t v2_kind(uint8_t opcode) {
  switch (opcode) {
    case AGINTI_V2_OPCODE_HELLO: return AGINTI_CMD_V2_HELLO;
    case AGINTI_V2_OPCODE_GET_CAPS: return AGINTI_CMD_V2_GET_CAPS;
    case AGINTI_V2_OPCODE_CONFIGURE: return AGINTI_CMD_V2_CONFIGURE;
    case AGINTI_V2_OPCODE_SINGLE_SHOT: return AGINTI_CMD_V2_SINGLE_SHOT;
    case AGINTI_V2_OPCODE_START_STREAM: return AGINTI_CMD_V2_START_STREAM;
    case AGINTI_V2_OPCODE_STOP_STREAM: return AGINTI_CMD_V2_STOP_STREAM;
    case AGINTI_V2_OPCODE_GET_STATUS: return AGINTI_CMD_V2_GET_STATUS;
    case AGINTI_V2_OPCODE_EEPROM_READ: return AGINTI_CMD_V2_EEPROM_READ;
    default: return AGINTI_CMD_NONE;
  }
}

static void parse_available(aginti_protocol_parser_t *parser) {
  while (parser->input_bytes > 0U) {
    if (parser->input[0] == 0x04U) {
      if (parser->input_bytes < 3U) return;
      if ((parser->input[1] == 0x0DU) && (parser->input[2] == 0x0AU)) {
        aginti_command_t command = {0};
        command.kind = AGINTI_CMD_IDENTITY;
        (void)queue_push(parser, &command);
        discard_prefix(parser, 3U);
      } else {
        ++parser->malformed_packets;
        discard_prefix(parser, 1U);
      }
      continue;
    }

    if (parser->input[0] == 0xFFU) {
      if (parser->input_bytes < 9U) return;
      if ((parser->input[7] != 0x0DU) || (parser->input[8] != 0x0AU)) {
        ++parser->malformed_packets;
        discard_prefix(parser, 1U);
        continue;
      }
      aginti_command_t command = {0};
      command.opcode = parser->input[1];
      command.output_mode = parser->input[2];
      command.exposure_clocks = read_le32(&parser->input[3]);
      switch (command.opcode) {
        case 0x08U: command.kind = AGINTI_CMD_LEGACY_EEPROM_WRITE; break;
        case 0x09U: command.kind = AGINTI_CMD_LEGACY_EEPROM_READ; break;
        case 0x10U: command.kind = AGINTI_CMD_LEGACY_CAL_READ; break;
        case 0x11U: command.kind = AGINTI_CMD_LEGACY_CAL_WRITE; break;
        default: command.kind = AGINTI_CMD_LEGACY_ACQUIRE; break;
      }
      (void)queue_push(parser, &command);
      discard_prefix(parser, 9U);
      continue;
    }

    if ((parser->input_bytes >= 4U) &&
        (memcmp(parser->input, "ASC2", 4U) == 0)) {
      if (parser->input_bytes < sizeof(aginti_v2_command_header_t)) return;
      const uint16_t payload_bytes = read_le16(&parser->input[12]);
      const uint16_t total = (uint16_t)(sizeof(aginti_v2_command_header_t) +
                                        payload_bytes);
      if ((payload_bytes > AGINTI_C12880_COMMAND_PAYLOAD_MAX) ||
          (total > sizeof(parser->input))) {
        ++parser->malformed_packets;
        discard_prefix(parser, 4U);
        continue;
      }
      if (parser->input_bytes < total) return;
      const uint32_t expected_crc = read_le32(&parser->input[16]);
      uint8_t copy[sizeof(parser->input)];
      memcpy(copy, parser->input, total);
      memset(&copy[16], 0, 4U);
      const uint32_t actual_crc = aginti_crc32(copy, total, 0U);
      if (expected_crc != actual_crc) {
        ++parser->crc_failures;
        discard_prefix(parser, total);
        continue;
      }
      aginti_command_t command = {0};
      command.kind = v2_kind(parser->input[5]);
      command.opcode = parser->input[5];
      command.sequence = read_le32(&parser->input[8]);
      command.payload_bytes = payload_bytes;
      if (payload_bytes != 0U) {
        memcpy(command.payload, &parser->input[20], payload_bytes);
      }
      if (command.kind == AGINTI_CMD_NONE) ++parser->malformed_packets;
      else (void)queue_push(parser, &command);
      discard_prefix(parser, total);
      continue;
    }

    if ((parser->input_bytes < 4U) &&
        ((parser->input[0] == 'A') || (parser->input[0] == 0xFFU))) {
      return;
    }
    ++parser->malformed_packets;
    discard_prefix(parser, 1U);
  }
}

void aginti_protocol_parser_feed(aginti_protocol_parser_t *parser,
                                 const uint8_t *data, size_t bytes) {
  if ((parser == NULL) || (data == NULL)) return;
  for (size_t i = 0; i < bytes; ++i) {
    if (parser->input_bytes == sizeof(parser->input)) {
      ++parser->malformed_packets;
      discard_prefix(parser, 1U);
    }
    parser->input[parser->input_bytes++] = data[i];
    parse_available(parser);
  }
}

bool aginti_protocol_parser_pop(aginti_protocol_parser_t *parser,
                                aginti_command_t *command) {
  if ((parser == NULL) || (command == NULL) ||
      (parser->queue_tail == parser->queue_head)) return false;
  *command = parser->queue[parser->queue_tail];
  parser->queue_tail = (uint8_t)((parser->queue_tail + 1U) & 3U);
  return true;
}

size_t aginti_v2_build_reply(uint8_t *destination, size_t capacity,
                             uint8_t opcode, uint32_t sequence,
                             uint16_t status, const void *payload,
                             uint16_t payload_bytes) {
  const size_t header_bytes = 20U;
  const size_t total = header_bytes + payload_bytes;
  if ((destination == NULL) || (capacity < total)) return 0U;
  memset(destination, 0, total);
  memcpy(destination, "ASR2", 4U);
  destination[4] = 2U;
  destination[5] = (uint8_t)(opcode | 0x80U);
  write_le16(&destination[6], status);
  write_le32(&destination[8], sequence);
  write_le16(&destination[12], payload_bytes);
  if ((payload_bytes != 0U) && (payload != NULL)) {
    memcpy(&destination[20], payload, payload_bytes);
  }
  write_le32(&destination[16], aginti_crc32(destination, total, 0U));
  return total;
}

