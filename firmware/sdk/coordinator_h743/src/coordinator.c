#include "aginti_coordinator.h"
#include "coordinator_board.h"

#include <string.h>

static const uint8_t seed_lut1[128] = {
    71,71,71,72,72,72,74,74,76,77,79,81,82,84,88,96,122,126,130,132,
    134,140,142,144,146,150,154,156,157,160,162,163,164,164,166,167,
    169,170,171,172,172,171,171,170,170,170,170,169,169,169,169,170,
    169,170,169,168,169,169,170,170,170,170,171,171,170,170,170,168,
    167,167,166,165,165,163,164,164,162,162,162,161,161,160,161,159,
    159,158,158,158,157,157,155,154,151,148,146,145,144,143,141,139,
    136,134,132,128,123,121,119,116,109,107,104,99,95,91,83,79,77,76,
    74,72,71,69,69,67,67,67,71,71};
static const uint8_t seed_lut2[128] = {
    194,194,194,194,194,194,193,192,192,192,191,190,190,189,188,188,
    209,208,207,206,206,205,204,203,202,201,200,200,197,195,194,192,
    189,187,185,182,178,174,171,168,165,160,156,151,148,143,138,134,
    131,128,124,122,119,116,114,112,109,109,108,106,106,105,103,103,
    102,102,102,103,103,103,104,105,105,107,109,110,112,114,117,120,
    123,125,129,132,137,139,143,147,150,153,155,158,160,163,165,168,
    169,172,173,174,176,178,178,179,179,180,181,181,181,181,181,181,
    182,183,183,184,185,185,186,187,187,187,188,189,189,189,194,194};

static bool deadline_reached(uint32_t now, uint32_t deadline) {
  return (int32_t)(now - deadline) >= 0;
}

static uint16_t seed_to_q16(uint8_t value) {
  return (uint16_t)(((uint32_t)value * 65535U + 127U) / 255U);
}

static void interpolate_lut(const aginti_coordinator_t *device,
                            uint16_t phase_q16, uint16_t *duty1,
                            uint16_t *duty2, uint16_t *nearest_index) {
  const uint32_t span = (uint32_t)(device->status.lut_count - 1U);
  const uint64_t position = (uint64_t)phase_q16 * span;
  const uint32_t index = (uint32_t)(position / 65535U);
  const uint32_t fraction = (uint32_t)(position % 65535U);
  const uint32_t next = (index < span) ? index + 1U : index;
  const uint32_t inv = 65535U - fraction;
  *duty1 = (uint16_t)(((uint64_t)device->lut1[index] * inv +
                       (uint64_t)device->lut1[next] * fraction + 32767U) /
                      65535U);
  *duty2 = (uint16_t)(((uint64_t)device->lut2[index] * inv +
                       (uint64_t)device->lut2[next] * fraction + 32767U) /
                      65535U);
  *nearest_index = (uint16_t)((fraction < 32768U) ? index : next);
}

static void enter_cooling(aginti_coordinator_t *device, uint32_t now) {
  coord_board_force_off();
  device->status.duty1_q16 = 0U;
  device->status.duty2_q16 = 0U;
  device->waiting_for_commit = false;
  device->waiting_for_trigger = false;
  device->armed = false;
  device->cooldown_deadline_us = now + device->config.cooldown_us;
  if (device->status.state != AGINTI_COORD_FAULT) {
    device->status.state = AGINTI_COORD_COOLING;
  }
}

static void enter_fault(aginti_coordinator_t *device, uint32_t fault,
                        uint32_t now) {
  device->status.faults |= fault;
  device->status.state = AGINTI_COORD_FAULT;
  enter_cooling(device, now);
}

void aginti_coord_init(aginti_coordinator_t *device) {
  memset(device, 0, sizeof(*device));
  device->config.cycle_us = 3000000U;
  device->config.settle_us = 5000U;
  device->config.cooldown_us = 5000000U;
  device->config.max_active_us = 10000000U;
  device->config.max_current_ma = 1500U;
  device->config.max_total_power_mw = 12000U;
  device->status.state = AGINTI_COORD_IDLE;
  device->status.lut_count = 128U;
  for (uint32_t i = 0U; i < 128U; ++i) {
    device->lut1[i] = seed_to_q16(seed_lut1[i]);
    device->lut2[i] = seed_to_q16(seed_lut2[i]);
  }
  coord_board_force_off();
}

bool aginti_coord_arm(aginti_coordinator_t *device) {
  if (device->status.state != AGINTI_COORD_IDLE ||
      device->status.faults != AGINTI_COORD_FAULT_NONE) return false;
  device->armed = true;
  device->status.state = AGINTI_COORD_ARMED;
  return true;
}

bool aginti_coord_start(aginti_coordinator_t *device, uint16_t cycles,
                        uint32_t cycle_us) {
  if (!device->armed || device->status.state != AGINTI_COORD_ARMED ||
      cycles == 0U || cycle_us < 100000U || cycle_us > 60000000U ||
      device->status.lut_count < 2U) return false;

  const uint32_t pwm_period = coord_board_pwm_period_us();
  uint32_t minimum_step = device->config.settle_us + 200U;
  if (minimum_step < pwm_period) minimum_step = pwm_period;
  uint32_t max_steps = (cycle_us / minimum_step) + 1U;
  if (max_steps < 2U) return false;
  if (max_steps > device->status.lut_count) max_steps = device->status.lut_count;

  device->config.cycle_us = cycle_us;
  device->status.effective_steps = (uint16_t)max_steps;
  device->status.step_index = 0U;
  device->status.cycle_index = 0U;
  device->status.cycles_total = cycles;
  device->run_started_us = coord_board_now_us();
  device->cycle_started_us = device->run_started_us;
  device->next_stage_us = device->run_started_us;
  device->waiting_for_commit = false;
  device->waiting_for_trigger = false;
  device->status.state = AGINTI_COORD_RUNNING;
  return true;
}

void aginti_coord_stop(aginti_coordinator_t *device) {
  const uint32_t now = coord_board_now_us();
  if (device->status.state == AGINTI_COORD_IDLE) {
    coord_board_force_off();
    device->armed = false;
    return;
  }
  enter_cooling(device, now);
}

bool aginti_coord_clear_fault(aginti_coordinator_t *device) {
  const uint32_t now = coord_board_now_us();
  if (device->status.state != AGINTI_COORD_FAULT ||
      !deadline_reached(now, device->cooldown_deadline_us)) return false;
  device->status.faults = AGINTI_COORD_FAULT_NONE;
  device->status.state = AGINTI_COORD_IDLE;
  return true;
}

bool aginti_coord_manual(aginti_coordinator_t *device, uint16_t duty1_q16,
                         uint16_t duty2_q16, uint32_t timeout_us) {
  if (!device->armed || device->status.state != AGINTI_COORD_ARMED ||
      timeout_us == 0U || timeout_us > device->config.max_active_us) return false;
  if (!coord_board_pwm_stage(duty1_q16, duty2_q16)) return false;
  device->status.duty1_q16 = duty1_q16;
  device->status.duty2_q16 = duty2_q16;
  device->waiting_for_commit = true;
  device->commit_deadline_us = coord_board_now_us() +
                               3U * coord_board_pwm_period_us() + 1000U;
  device->manual_deadline_us = coord_board_now_us() + timeout_us;
  device->run_started_us = coord_board_now_us();
  device->status.state = AGINTI_COORD_MANUAL;
  return true;
}

bool aginti_coord_set_lut_length(aginti_coordinator_t *device, uint16_t count) {
  if (device->status.state != AGINTI_COORD_IDLE &&
      device->status.state != AGINTI_COORD_ARMED) return false;
  if (count < 2U || count > AGINTI_COORD_LUT_CAPACITY) return false;
  device->status.lut_count = count;
  return true;
}

bool aginti_coord_set_lut_entry(aginti_coordinator_t *device, uint16_t index,
                                uint16_t duty1_q16, uint16_t duty2_q16) {
  if (device->status.state != AGINTI_COORD_IDLE &&
      device->status.state != AGINTI_COORD_ARMED) return false;
  if (index >= AGINTI_COORD_LUT_CAPACITY) return false;
  device->lut1[index] = duty1_q16;
  device->lut2[index] = duty2_q16;
  return true;
}

bool aginti_coord_set_timing(aginti_coordinator_t *device,
                             uint32_t settle_us, uint32_t cooldown_us,
                             uint32_t max_active_us) {
  if (device->status.state != AGINTI_COORD_IDLE &&
      device->status.state != AGINTI_COORD_ARMED) return false;
  if (settle_us > 1000000U || cooldown_us < 1000000U ||
      max_active_us < 100000U || max_active_us > 60000000U) return false;
  device->config.settle_us = settle_us;
  device->config.cooldown_us = cooldown_us;
  device->config.max_active_us = max_active_us;
  return true;
}

void aginti_coord_guard_telemetry(aginti_coordinator_t *device,
                                  const aginti_coord_telemetry_t *telemetry) {
  if (device->status.state != AGINTI_COORD_RUNNING &&
      device->status.state != AGINTI_COORD_MANUAL) return;
  const uint32_t now = coord_board_now_us();
  if (((telemetry->valid_flags & AGINTI_TELEM_INA1) != 0U &&
       telemetry->current1_ma > device->config.max_current_ma) ||
      ((telemetry->valid_flags & AGINTI_TELEM_INA2) != 0U &&
       telemetry->current2_ma > device->config.max_current_ma)) {
    enter_fault(device, AGINTI_COORD_FAULT_OVERCURRENT, now);
    return;
  }
  if ((telemetry->valid_flags & (AGINTI_TELEM_INA1 | AGINTI_TELEM_INA2)) ==
          (AGINTI_TELEM_INA1 | AGINTI_TELEM_INA2) &&
      telemetry->power1_mw + telemetry->power2_mw >
          device->config.max_total_power_mw) {
    enter_fault(device, AGINTI_COORD_FAULT_OVERPOWER, now);
  }
}

void aginti_coord_poll(aginti_coordinator_t *device) {
  const uint32_t now = coord_board_now_us();
  uint32_t committed_us = 0U;

  if (device->status.state == AGINTI_COORD_COOLING &&
      deadline_reached(now, device->cooldown_deadline_us)) {
    device->status.state = AGINTI_COORD_IDLE;
    return;
  }
  if (device->status.state != AGINTI_COORD_RUNNING &&
      device->status.state != AGINTI_COORD_MANUAL) return;

  if ((uint32_t)(now - device->run_started_us) > device->config.max_active_us) {
    enter_fault(device, AGINTI_COORD_FAULT_MAX_ACTIVE_TIME, now);
    return;
  }

  if (device->waiting_for_commit && coord_board_pwm_take_commit(&committed_us)) {
    device->waiting_for_commit = false;
    if (device->status.state == AGINTI_COORD_RUNNING) {
      device->waiting_for_trigger = true;
      device->trigger_due_us = committed_us + device->config.settle_us;
    }
  } else if (device->waiting_for_commit &&
             deadline_reached(now, device->commit_deadline_us)) {
    enter_fault(device, AGINTI_COORD_FAULT_PWM_COMMIT_TIMEOUT, now);
    return;
  }

  if (device->status.state == AGINTI_COORD_MANUAL) {
    if (deadline_reached(now, device->manual_deadline_us)) enter_cooling(device, now);
    return;
  }

  if (device->waiting_for_trigger && deadline_reached(now, device->trigger_due_us)) {
    coord_board_trigger_pulse(10U);
    device->status.trigger_sequence++;
    coord_board_send_sync(device->status.trigger_sequence,
                          device->trigger_due_us, device->status.lut_index,
                          device->status.duty1_q16,
                          device->status.duty2_q16);
    device->waiting_for_trigger = false;
    device->status.step_index++;
    if (device->status.step_index >= device->status.effective_steps) {
      device->status.cycle_index++;
      if (device->status.cycle_index >= device->status.cycles_total) {
        enter_cooling(device, now);
        return;
      }
      device->status.step_index = 0U;
      device->cycle_started_us += device->config.cycle_us;
    }
    const uint32_t denominator = device->status.effective_steps - 1U;
    device->next_stage_us = device->cycle_started_us + (uint32_t)(
        ((uint64_t)device->status.step_index * device->config.cycle_us) /
            denominator);
  }

  if (!device->waiting_for_commit && !device->waiting_for_trigger &&
      deadline_reached(now, device->next_stage_us)) {
    const uint32_t denominator = device->status.effective_steps - 1U;
    const uint16_t phase = (uint16_t)(
        ((uint32_t)device->status.step_index * 65535U) / denominator);
    uint16_t duty1 = 0U, duty2 = 0U, index = 0U;
    interpolate_lut(device, phase, &duty1, &duty2, &index);
    if ((uint32_t)(now - device->next_stage_us) >
        2U * coord_board_pwm_period_us()) {
      device->status.faults |= AGINTI_COORD_WARN_SCHEDULE_LATE;
      device->status.schedule_late_count++;
    }
    if (!coord_board_pwm_stage(duty1, duty2)) {
      enter_fault(device, AGINTI_COORD_FAULT_BAD_CONFIG, now);
      return;
    }
    device->status.duty1_q16 = duty1;
    device->status.duty2_q16 = duty2;
    device->status.lut_index = index;
    device->waiting_for_commit = true;
    device->commit_deadline_us = now +
        3U * coord_board_pwm_period_us() + 1000U;
  }
}

const aginti_coord_status_t *aginti_coord_status(
    const aginti_coordinator_t *device) { return &device->status; }
const aginti_coord_config_t *aginti_coord_config(
    const aginti_coordinator_t *device) { return &device->config; }

const char *aginti_coord_state_name(aginti_coord_state_t state) {
  static const char *const names[] = {
      "IDLE", "ARMED", "RUNNING", "MANUAL", "COOLING", "FAULT"};
  return ((uint32_t)state < 6U) ? names[state] : "UNKNOWN";
}
