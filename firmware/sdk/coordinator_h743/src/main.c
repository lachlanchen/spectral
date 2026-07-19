#include "aginti_coordinator.h"
#include "coordinator_board.h"

#include "stm32h7xx_hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static aginti_coordinator_t coordinator;
static bool telemetry_enabled = true;

void SysTick_Handler(void) { HAL_IncTick(); }
void HardFault_Handler(void) { coord_board_panic(1U); }
void MemManage_Handler(void) { coord_board_panic(2U); }
void BusFault_Handler(void) { coord_board_panic(3U); }
void UsageFault_Handler(void) { coord_board_panic(4U); }
void assert_failed(uint8_t *file, uint32_t line) {
  (void)file;
  coord_board_panic(0xA5000000U | (line & 0x00FFFFFFU));
}

static void write_status(void) {
  char line[256];
  const aginti_coord_status_t *s = aginti_coord_status(&coordinator);
  const aginti_coord_config_t *c = aginti_coord_config(&coordinator);
  (void)snprintf(line, sizeof(line),
      "STATUS state=%s faults=0x%08lX seq=%lu duty=%u,%u lut=%u/%u "
      "step=%u/%u cycle=%u/%u period_us=%lu settle_us=%lu probes=0x%02lX\r\n",
      aginti_coord_state_name(s->state), (unsigned long)s->faults,
      (unsigned long)s->trigger_sequence, s->duty1_q16, s->duty2_q16,
      s->lut_index, s->lut_count, s->step_index, s->effective_steps,
      s->cycle_index, s->cycles_total, (unsigned long)c->cycle_us,
      (unsigned long)c->settle_us,
      (unsigned long)coord_board_probe_flags());
  coord_board_host_write(line);
}

static void write_help(void) {
  coord_board_host_write(
      "COMMANDS\r\n"
      "  STATUS | HELP | ARM | STOP | CLEAR\r\n"
      "  RUN <cycles> <period_ms>\r\n"
      "  MANUAL <duty1_q16> <duty2_q16> <timeout_ms>\r\n"
      "  LUTLEN <2..256>\r\n"
      "  LUT <index> <duty1_q16> <duty2_q16>\r\n"
      "  TIMING <settle_us> <cooldown_ms> <max_on_ms>\r\n"
      "  TELEM <0|1>\r\n");
}

static void handle_command(char *line) {
  unsigned long a = 0UL, b = 0UL, c = 0UL;
  if (strcmp(line, "STATUS") == 0) { write_status(); return; }
  if (strcmp(line, "HELP") == 0 || strcmp(line, "?") == 0) { write_help(); return; }
  if (strcmp(line, "ARM") == 0) {
    coord_board_host_write(aginti_coord_arm(&coordinator) ? "OK ARMED\r\n" : "ERR ARM\r\n");
    return;
  }
  if (strcmp(line, "STOP") == 0 || strcmp(line, "OFF") == 0) {
    aginti_coord_stop(&coordinator); coord_board_host_write("OK OFF\r\n"); return;
  }
  if (strcmp(line, "CLEAR") == 0) {
    coord_board_host_write(aginti_coord_clear_fault(&coordinator) ? "OK CLEARED\r\n" : "ERR CLEAR\r\n");
    return;
  }
  if (sscanf(line, "RUN %lu %lu", &a, &b) == 2) {
    const bool ok = a <= 65535UL && b <= 60000UL &&
                    aginti_coord_start(&coordinator, (uint16_t)a,
                                       (uint32_t)b * 1000U);
    coord_board_host_write(ok ? "OK RUN\r\n" : "ERR RUN\r\n"); return;
  }
  if (sscanf(line, "MANUAL %lu %lu %lu", &a, &b, &c) == 3) {
    const bool ok = a <= 65535UL && b <= 65535UL && c <= 60000UL &&
        aginti_coord_manual(&coordinator, (uint16_t)a, (uint16_t)b,
                            (uint32_t)c * 1000U);
    coord_board_host_write(ok ? "OK MANUAL\r\n" : "ERR MANUAL\r\n"); return;
  }
  if (sscanf(line, "LUTLEN %lu", &a) == 1) {
    const bool ok = a <= 65535UL && aginti_coord_set_lut_length(&coordinator, (uint16_t)a);
    coord_board_host_write(ok ? "OK LUTLEN\r\n" : "ERR LUTLEN\r\n"); return;
  }
  if (sscanf(line, "LUT %lu %lu %lu", &a, &b, &c) == 3) {
    const bool ok = a < AGINTI_COORD_LUT_CAPACITY && b <= 65535UL && c <= 65535UL &&
        aginti_coord_set_lut_entry(&coordinator, (uint16_t)a, (uint16_t)b, (uint16_t)c);
    coord_board_host_write(ok ? "OK LUT\r\n" : "ERR LUT\r\n"); return;
  }
  if (sscanf(line, "TIMING %lu %lu %lu", &a, &b, &c) == 3) {
    const bool ok = b <= 60000UL && c <= 60000UL &&
        aginti_coord_set_timing(&coordinator, (uint32_t)a,
                                (uint32_t)b * 1000U, (uint32_t)c * 1000U);
    coord_board_host_write(ok ? "OK TIMING\r\n" : "ERR TIMING\r\n"); return;
  }
  if (sscanf(line, "TELEM %lu", &a) == 1 && a <= 1UL) {
    telemetry_enabled = a != 0UL; coord_board_host_write("OK TELEM\r\n"); return;
  }
  coord_board_host_write("ERR UNKNOWN\r\n");
}

static void write_telemetry(const aginti_coord_telemetry_t *t) {
  char line[384];
  const aginti_coord_status_t *s = aginti_coord_status(&coordinator);
  uint32_t spectral_sum = 0U;
  for (uint32_t i = 0U; i < 18U; ++i) spectral_sum += t->as7343[i];
  const uint16_t visible = (t->tsl_ch0 > t->tsl_ch1)
                               ? (uint16_t)(t->tsl_ch0 - t->tsl_ch1) : 0U;
  (void)snprintf(line, sizeof(line),
      "T,%lu,%s,%u,%u,0x%02lX,%u,%u,%lu,%u,%u,%lu,%u,%u,%u,%lu\r\n",
      (unsigned long)t->timestamp_us, aginti_coord_state_name(s->state),
      s->duty1_q16, s->duty2_q16, (unsigned long)t->valid_flags,
      t->bus1_mv, t->current1_ma, (unsigned long)t->power1_mw,
      t->bus2_mv, t->current2_ma, (unsigned long)t->power2_mw,
      t->tsl_ch0, t->tsl_ch1, visible, (unsigned long)spectral_sum);
  coord_board_host_write(line);
}

int main(void) {
  if (!coord_board_init()) coord_board_panic(0x100U);
  aginti_coord_init(&coordinator);
  coord_board_host_write("AgInTi dual-H7 coordinator v0.1\r\n");
  coord_board_host_write("SAFE_IDLE lamps=OFF; type HELP\r\n");
  write_status();

  char command[128];
  size_t command_bytes = 0U;
  uint32_t last_telemetry_us = coord_board_now_us();
  while (1) {
    uint8_t byte = 0U;
    while (coord_board_host_read_byte(&byte)) {
      if (byte == '\r' || byte == '\n') {
        if (command_bytes > 0U) {
          command[command_bytes] = '\0';
          handle_command(command);
          command_bytes = 0U;
        }
      } else if (command_bytes + 1U < sizeof(command)) {
        command[command_bytes++] = (char)byte;
      } else {
        command_bytes = 0U;
      }
    }

    aginti_coord_poll(&coordinator);
    const uint32_t now = coord_board_now_us();
    if ((uint32_t)(now - last_telemetry_us) >= 100000U) {
      aginti_coord_telemetry_t telemetry;
      if (coord_board_read_telemetry(&telemetry)) {
        aginti_coord_guard_telemetry(&coordinator, &telemetry);
        if (telemetry_enabled) write_telemetry(&telemetry);
      }
      last_telemetry_us = now;
    }
  }
}
