#ifndef AGINTI_COORDINATOR_H
#define AGINTI_COORDINATOR_H

#include <stdbool.h>
#include <stdint.h>

#define AGINTI_COORD_LUT_CAPACITY 256U

typedef enum {
  AGINTI_COORD_IDLE = 0,
  AGINTI_COORD_ARMED,
  AGINTI_COORD_RUNNING,
  AGINTI_COORD_MANUAL,
  AGINTI_COORD_COOLING,
  AGINTI_COORD_FAULT
} aginti_coord_state_t;

enum {
  AGINTI_COORD_FAULT_NONE = 0U,
  AGINTI_COORD_FAULT_BAD_CONFIG = 1U << 0,
  AGINTI_COORD_FAULT_PWM_COMMIT_TIMEOUT = 1U << 1,
  AGINTI_COORD_FAULT_MAX_ACTIVE_TIME = 1U << 2,
  AGINTI_COORD_FAULT_OVERCURRENT = 1U << 3,
  AGINTI_COORD_FAULT_OVERPOWER = 1U << 4,
  AGINTI_COORD_WARN_SCHEDULE_LATE = 1U << 16
};

enum {
  AGINTI_TELEM_INA1 = 1U << 0,
  AGINTI_TELEM_INA2 = 1U << 1,
  AGINTI_TELEM_TSL2591 = 1U << 2,
  AGINTI_TELEM_AS7343 = 1U << 3
};

typedef struct {
  uint32_t timestamp_us;
  uint32_t valid_flags;
  uint16_t bus1_mv;
  uint16_t bus2_mv;
  uint16_t current1_ma;
  uint16_t current2_ma;
  uint32_t power1_mw;
  uint32_t power2_mw;
  uint16_t tsl_ch0;
  uint16_t tsl_ch1;
  uint16_t as7343[18];
} aginti_coord_telemetry_t;

typedef struct {
  uint32_t cycle_us;
  uint32_t settle_us;
  uint32_t cooldown_us;
  uint32_t max_active_us;
  uint16_t max_current_ma;
  uint16_t reserved;
  uint32_t max_total_power_mw;
} aginti_coord_config_t;

typedef struct {
  aginti_coord_state_t state;
  uint32_t faults;
  uint32_t trigger_sequence;
  uint32_t schedule_late_count;
  uint16_t duty1_q16;
  uint16_t duty2_q16;
  uint16_t lut_index;
  uint16_t lut_count;
  uint16_t effective_steps;
  uint16_t step_index;
  uint16_t cycle_index;
  uint16_t cycles_total;
} aginti_coord_status_t;

typedef struct {
  aginti_coord_config_t config;
  aginti_coord_status_t status;
  uint16_t lut1[AGINTI_COORD_LUT_CAPACITY];
  uint16_t lut2[AGINTI_COORD_LUT_CAPACITY];
  uint32_t run_started_us;
  uint32_t cycle_started_us;
  uint32_t next_stage_us;
  uint32_t commit_deadline_us;
  uint32_t trigger_due_us;
  uint32_t manual_deadline_us;
  uint32_t cooldown_deadline_us;
  bool waiting_for_commit;
  bool waiting_for_trigger;
  bool armed;
} aginti_coordinator_t;

void aginti_coord_init(aginti_coordinator_t *device);
bool aginti_coord_arm(aginti_coordinator_t *device);
bool aginti_coord_start(aginti_coordinator_t *device, uint16_t cycles,
                        uint32_t cycle_us);
void aginti_coord_stop(aginti_coordinator_t *device);
bool aginti_coord_clear_fault(aginti_coordinator_t *device);
bool aginti_coord_manual(aginti_coordinator_t *device, uint16_t duty1_q16,
                         uint16_t duty2_q16, uint32_t timeout_us);
bool aginti_coord_set_lut_length(aginti_coordinator_t *device, uint16_t count);
bool aginti_coord_set_lut_entry(aginti_coordinator_t *device, uint16_t index,
                                uint16_t duty1_q16, uint16_t duty2_q16);
bool aginti_coord_set_timing(aginti_coordinator_t *device,
                             uint32_t settle_us, uint32_t cooldown_us,
                             uint32_t max_active_us);
void aginti_coord_guard_telemetry(aginti_coordinator_t *device,
                                  const aginti_coord_telemetry_t *telemetry);
void aginti_coord_poll(aginti_coordinator_t *device);
const aginti_coord_status_t *aginti_coord_status(
    const aginti_coordinator_t *device);
const aginti_coord_config_t *aginti_coord_config(
    const aginti_coordinator_t *device);
const char *aginti_coord_state_name(aginti_coord_state_t state);

#endif
