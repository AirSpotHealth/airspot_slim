/*
 * Copyright (c) 2026 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * LED Indicator Service Implementation
 *
 * Non-blocking LED patterns using Zephyr work queue and timers.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/led_driver.h>
#include <services/led_indicator.h>
#include <services/user_settings.h>

LOG_MODULE_REGISTER(led_indicator, LOG_LEVEL_INF);

/* Pattern timing */
#define FADE_STEP_MS 20 /* 50 updates/second for smooth fade */
#define FADE_STEPS 50   /* Number of brightness steps per fade cycle */

/* Pattern state */
typedef struct {
  led_pattern_t pattern;
  led_color_t color;
  uint32_t period_ms;
  uint32_t flash_on_ms; /* On-time for flash pattern (0 = 50%) */
  uint32_t duration_ms;
  int64_t start_time;
  bool continuous;    /* If true, pattern runs until stopped */
  uint8_t brightness; /* Current brightness 0-100 */
  bool fade_up;       /* Fade direction */

  /* Sequence state for multi-phase patterns */
  uint8_t sequence_phase;
  led_color_t sequence_color2;
  uint32_t sequence_duration2;
} pattern_state_t;

static pattern_state_t state;
static bool initialized = false;
static struct k_work_delayable pattern_work;
static struct k_mutex state_mutex;

/* Forward declarations */
static void pattern_work_handler(struct k_work *work);
static void set_color(led_color_t color, uint8_t brightness);
static void stop_pattern_internal(void);

int led_indicator_init(void) {
  if (initialized) {
    return 0;
  }

  k_mutex_init(&state_mutex);
  k_work_init_delayable(&pattern_work, pattern_work_handler);

  memset(&state, 0, sizeof(state));
  state.pattern = LED_PATTERN_OFF;

  initialized = true;
  LOG_INF("LED indicator service initialized");
  return 0;
}

void led_indicator_stop(void) {
  k_mutex_lock(&state_mutex, K_FOREVER);
  stop_pattern_internal();
  k_mutex_unlock(&state_mutex);
}

static void stop_pattern_internal(void) {
  k_work_cancel_delayable(&pattern_work);
  state.pattern = LED_PATTERN_OFF;
  state.continuous = false;
  state.sequence_phase = 0;
  set_color(LED_COLOR_OFF, 0);
}

bool led_indicator_is_active(void) { return state.pattern != LED_PATTERN_OFF; }

/* ========================================
 * Color Helper Functions
 * ======================================== */

/* Rev3A LEDs are on P2, outside PWM20/21/22's P1 routing. Use a
 * short timer interrupt to give green 6% duty only while amber is visible.
 * No busy-wait or workqueue dependency; stop it during flash gaps and off. */
static struct k_spinlock color_lock;
static bool amber_pwm_active;
static bool amber_green_on;
#define AMBER_GREEN_ON_US 192
#define AMBER_GREEN_OFF_US 3008

static void amber_pwm_tick(struct k_timer *timer) {
  k_spinlock_key_t key = k_spin_lock(&color_lock);
  if (amber_pwm_active) {
    amber_green_on = !amber_green_on;
    led_driver_set_brightness(LED_GREEN, amber_green_on ? 100 : 0);
    k_timer_start(timer,
                  K_USEC(amber_green_on ? AMBER_GREEN_ON_US : AMBER_GREEN_OFF_US),
                  K_NO_WAIT);
  }
  k_spin_unlock(&color_lock, key);
}

K_TIMER_DEFINE(amber_pwm_timer, amber_pwm_tick, NULL);

static void set_color(led_color_t color, uint8_t brightness) {
  k_spinlock_key_t key = k_spin_lock(&color_lock);
  bool amber = color == LED_COLOR_AMBER && brightness > 0;
  if (amber) {
    led_driver_set_brightness(LED_RED, brightness);
    led_driver_set_brightness(LED_BLUE, 0);
    if (!amber_pwm_active) {
      amber_pwm_active = true;
      amber_green_on = true;
      led_driver_set_brightness(LED_GREEN, 100);
      /* 192 us on + 3008 us off: 312.5 Hz, 6% green.
       * Both intervals align with the production 32 us kernel tick.
       * Two callbacks per cycle, with no timer running between LED flashes. */
      k_timer_start(&amber_pwm_timer, K_USEC(AMBER_GREEN_ON_US), K_NO_WAIT);
    }
    k_spin_unlock(&color_lock, key);
    return;
  }

  amber_pwm_active = false;
  k_timer_stop(&amber_pwm_timer);
  led_driver_set_brightness(LED_RED, 0);
  led_driver_set_brightness(LED_GREEN, 0);
  led_driver_set_brightness(LED_BLUE, 0);

  switch (color) {
  case LED_COLOR_RED:
    led_driver_set_brightness(LED_RED, brightness);
    break;
  case LED_COLOR_GREEN:
    led_driver_set_brightness(LED_GREEN, brightness);
    break;
  case LED_COLOR_BLUE:
    led_driver_set_brightness(LED_BLUE, brightness);
    break;
  case LED_COLOR_WHITE:
    led_driver_set_brightness(LED_RED, brightness);
    led_driver_set_brightness(LED_GREEN, brightness);
    led_driver_set_brightness(LED_BLUE, brightness);
    break;
  default:
    break;
  }
  k_spin_unlock(&color_lock, key);
}

/* ========================================
 * Blocking Software-PWM Ramp
 * ======================================== */

/* The gpio-leds have no hardware dimming (set_brightness is on/off only), so we
 * render intermediate brightness by bit-banging the channels on/off at a carrier
 * fast enough to be flicker-free. One-shot, blocking — for power on/off cues. */
#define LED_PWM_CARRIER_US 2000 /* 500 Hz carrier — no visible flicker */
#define LED_RAMP_FRAME_MS 20    /* per-step render window (~50 steps/s) */

/* Drive `color` at `duty_pct` for `window_ms` via software PWM (busy-wait). */
static void pwm_render_frame(led_color_t color, uint8_t duty_pct,
                             uint32_t window_ms) {
  if (duty_pct > 100) {
    duty_pct = 100;
  }
  uint32_t on_us = (LED_PWM_CARRIER_US * duty_pct) / 100u;
  uint32_t off_us = LED_PWM_CARRIER_US - on_us;
  uint32_t cycles = (window_ms * 1000u) / LED_PWM_CARRIER_US;
  if (cycles == 0) {
    cycles = 1;
  }

  for (uint32_t c = 0; c < cycles; c++) {
    if (on_us) {
      set_color(color, 100);
      k_busy_wait(on_us);
    }
    if (off_us) {
      set_color(color, 0);
      k_busy_wait(off_us);
    }
  }
}

bool led_indicator_ramp(led_color_t color, uint8_t from_pct, uint8_t to_pct,
                        uint32_t duration_ms,
                        led_ramp_continue_cb should_continue) {
  /* Take the LED away from any running pattern for the duration of the ramp. */
  led_indicator_stop();

  uint32_t steps = duration_ms / LED_RAMP_FRAME_MS;
  if (steps == 0) {
    steps = 1;
  }

  for (uint32_t s = 0; s <= steps; s++) {
    if (should_continue && !should_continue()) {
      set_color(color, 0);
      return false; /* aborted */
    }

    /* Linear progress, then gamma-corrected (square) so the perceived change is
     * even rather than crowding the bright end. */
    int lin = (int)from_pct +
              ((int)to_pct - (int)from_pct) * (int)s / (int)steps;
    if (lin < 0) {
      lin = 0;
    } else if (lin > 100) {
      lin = 100;
    }
    uint8_t duty = (uint8_t)((lin * lin) / 100);

    pwm_render_frame(color, duty, LED_RAMP_FRAME_MS);
  }

  set_color(color, to_pct ? 100 : 0); /* settle at the endpoint */
  return true;
}

/* ========================================
 * CO2 Level Helpers
 * ======================================== */

co2_level_t led_indicator_get_co2_level(uint16_t co2_ppm) {
  uint16_t yellow_start = user_settings_get_co2_yellow_start();
  uint16_t red_start = user_settings_get_co2_red_start();

  if (co2_ppm < yellow_start) {
    return CO2_LEVEL_GOOD;
  } else if (co2_ppm < red_start) {
    return CO2_LEVEL_MODERATE;
  } else {
    return CO2_LEVEL_POOR;
  }
}

led_color_t led_indicator_get_co2_color(uint16_t co2_ppm) {
  co2_level_t level = led_indicator_get_co2_level(co2_ppm);
  switch (level) {
  case CO2_LEVEL_GOOD:
    return LED_COLOR_GREEN;
  case CO2_LEVEL_MODERATE:
    return LED_COLOR_AMBER;
  case CO2_LEVEL_POOR:
    return LED_COLOR_RED;
  default:
    return LED_COLOR_GREEN;
  }
}

/* ========================================
 * Pattern Work Handler
 * ======================================== */

static void pattern_work_handler(struct k_work *work) {
  ARG_UNUSED(work);

  k_mutex_lock(&state_mutex, K_FOREVER);

  if (state.pattern == LED_PATTERN_OFF) {
    k_mutex_unlock(&state_mutex);
    return;
  }

  int64_t now = k_uptime_get();
  int64_t elapsed = now - state.start_time;

  /* Check if timed pattern has expired */
  if (!state.continuous && state.duration_ms > 0 &&
      elapsed >= state.duration_ms) {
    /* Check for sequence continuation */
    if (state.sequence_phase == 1 && state.sequence_duration2 > 0) {
      /* Move to phase 2 */
      state.sequence_phase = 2;
      state.color = state.sequence_color2;
      state.duration_ms = state.sequence_duration2;
      state.start_time = now;
      state.sequence_duration2 = 0; /* Clear so we don't repeat */
      LOG_DBG("LED sequence phase 2: color=%d, duration=%u", state.color,
              state.duration_ms);
    } else {
      LOG_DBG("LED pattern complete");
      stop_pattern_internal();
      k_mutex_unlock(&state_mutex);
      return;
    }
  }

  /* Process pattern */
  switch (state.pattern) {
  case LED_PATTERN_SOLID:
    set_color(state.color, 100);
    /* Schedule next check for timeout */
    if (!state.continuous && state.duration_ms > 0) {
      uint32_t remaining = state.duration_ms - (uint32_t)elapsed;
      k_work_schedule(&pattern_work, K_MSEC(remaining));
    }
    break;

  case LED_PATTERN_FLASH: {
    /* Calculate flash state based on elapsed time */
    uint32_t phase = (elapsed % state.period_ms);
    uint32_t on_time = state.flash_on_ms > 0 ? state.flash_on_ms
                                            : (state.period_ms / 2);
    if (on_time > state.period_ms) {
      on_time = state.period_ms;
    }
    bool led_on = phase < on_time;
    set_color(state.color, led_on ? 100 : 0);

    /* Schedule next toggle */
    uint32_t next_ms = led_on ? (on_time - phase) : (state.period_ms - phase);
    if (next_ms < 10)
      next_ms = 10; /* Minimum delay */
    k_work_schedule(&pattern_work, K_MSEC(next_ms));
    break;
  }

  case LED_PATTERN_FADE: {
    /* Smooth fade using sine-like curve */
    uint32_t cycle_pos = elapsed % state.period_ms;
    uint32_t half_period = state.period_ms / 2;

    uint8_t brightness;
    if (cycle_pos < half_period) {
      /* Fading up */
      brightness = (uint8_t)((cycle_pos * 100) / half_period);
    } else {
      /* Fading down */
      brightness =
          (uint8_t)(((state.period_ms - cycle_pos) * 100) / half_period);
    }

    set_color(state.color, brightness);
    k_work_schedule(&pattern_work, K_MSEC(FADE_STEP_MS));
    break;
  }

  case LED_PATTERN_FADE_PULSE: {
    /* Smooth fade in/out over flash_on_ms, then rest (off) until the next
     * period. Updates frequently during the fade, then sleeps through the
     * resting gap to save power. */
    uint32_t cycle_pos = elapsed % state.period_ms;
    uint32_t window = state.flash_on_ms > 0 ? state.flash_on_ms : 1600;
    if (window > state.period_ms) {
      window = state.period_ms;
    }

    if (cycle_pos < window) {
      uint32_t half = window / 2;
      uint8_t brightness;
      if (half == 0) {
        brightness = 100;
      } else if (cycle_pos < half) {
        brightness = (uint8_t)((cycle_pos * 100) / half); /* fade up */
      } else {
        brightness = (uint8_t)(((window - cycle_pos) * 100) / half); /* down */
      }
      set_color(state.color, brightness);
      k_work_schedule(&pattern_work, K_MSEC(FADE_STEP_MS));
    } else {
      /* Resting: LED off, sleep until the start of the next period. */
      set_color(state.color, 0);
      k_work_schedule(&pattern_work, K_MSEC(state.period_ms - cycle_pos));
    }
    break;
  }

  case LED_PATTERN_PULSE:
    /* Quick pulse - brightness ramp */
    if (state.fade_up) {
      state.brightness += 10;
      if (state.brightness >= 100) {
        state.brightness = 100;
        state.fade_up = false;
      }
    } else {
      if (state.brightness >= 10) {
        state.brightness -= 10;
      } else {
        state.brightness = 0;
        stop_pattern_internal();
        k_mutex_unlock(&state_mutex);
        return;
      }
    }
    set_color(state.color, state.brightness);
    k_work_schedule(&pattern_work, K_MSEC(20));
    break;

  default:
    break;
  }

  k_mutex_unlock(&state_mutex);
}

/* ========================================
 * Pattern Start Functions
 * ======================================== */

static void start_pattern(led_pattern_t pattern, led_color_t color,
                          uint32_t period_ms, uint32_t duration_ms,
                          bool continuous) {
  k_mutex_lock(&state_mutex, K_FOREVER);

  /* Stop any existing pattern */
  k_work_cancel_delayable(&pattern_work);
  set_color(LED_COLOR_OFF, 0);

  /* Set new pattern state */
  state.pattern = pattern;
  state.color = color;
  state.period_ms = period_ms > 0 ? period_ms : LED_FLASH_PERIOD_MS;
  state.flash_on_ms = 0;
  state.duration_ms = duration_ms;
  state.start_time = k_uptime_get();
  state.continuous = continuous;
  state.brightness = 0;
  state.fade_up = true;
  state.sequence_phase = 0;
  state.sequence_color2 = LED_COLOR_OFF;
  state.sequence_duration2 = 0;

  LOG_DBG("LED pattern start: p=%d c=%d period=%u dur=%u cont=%d", pattern,
          color, period_ms, duration_ms, continuous);

  /* Start immediately */
  k_work_schedule(&pattern_work, K_NO_WAIT);

  k_mutex_unlock(&state_mutex);
}

/* ========================================
 * Public API - Timed Patterns
 * ======================================== */

void led_indicator_show_settling_tick(void) {
  k_mutex_lock(&state_mutex, K_FOREVER);
  if (state.pattern == LED_PATTERN_OFF) {
    /* Zephyr mutexes are recursive: keep the idle check and start atomic. */
    start_pattern(LED_PATTERN_SOLID, LED_COLOR_WHITE, 0, 100, false);
  }
  k_mutex_unlock(&state_mutex);
}

void led_indicator_show_color(led_color_t color, uint32_t duration_ms) {
  start_pattern(LED_PATTERN_SOLID, color, 0, duration_ms, duration_ms == 0);
}

void led_indicator_flash(led_color_t color, uint32_t period_ms,
                         uint32_t duration_ms) {
  start_pattern(LED_PATTERN_FLASH, color, period_ms, duration_ms,
                duration_ms == 0);
}

void led_indicator_fade(led_color_t color, uint32_t duration_ms) {
  /* Use duration as both period and total time for single fade cycle */
  start_pattern(LED_PATTERN_FADE, color, duration_ms, duration_ms, false);
}

/* ========================================
 * Public API - Continuous Patterns
 * ======================================== */

void led_indicator_start_flash(led_color_t color, uint32_t period_ms) {
  start_pattern(LED_PATTERN_FLASH, color, period_ms, 0, true);
}

void led_indicator_start_flash_pulse(led_color_t color, uint32_t period_ms,
                                     uint32_t pulse_ms) {
  k_mutex_lock(&state_mutex, K_FOREVER);

  /* Stop any existing pattern */
  k_work_cancel_delayable(&pattern_work);
  set_color(LED_COLOR_OFF, 0);

  /* Set new pattern state */
  state.pattern = LED_PATTERN_FLASH;
  state.color = color;
  state.period_ms = period_ms > 0 ? period_ms : LED_FLASH_PERIOD_MS;
  state.flash_on_ms = pulse_ms;
  state.duration_ms = 0;
  state.start_time = k_uptime_get();
  state.continuous = true;
  state.brightness = 0;
  state.fade_up = true;
  state.sequence_phase = 0;
  state.sequence_color2 = LED_COLOR_OFF;
  state.sequence_duration2 = 0;

  LOG_DBG("LED flash pulse: color=%d period=%u on=%u", color, period_ms,
          pulse_ms);

  /* Start immediately */
  k_work_schedule(&pattern_work, K_NO_WAIT);

  k_mutex_unlock(&state_mutex);
}

void led_indicator_start_fade(led_color_t color, uint32_t period_ms) {
  start_pattern(LED_PATTERN_FADE, color, period_ms, 0, true);
}

void led_indicator_start_fade_pulse(led_color_t color, uint32_t period_ms,
                                    uint32_t fade_ms) {
  k_mutex_lock(&state_mutex, K_FOREVER);

  /* Stop any existing pattern */
  k_work_cancel_delayable(&pattern_work);
  set_color(LED_COLOR_OFF, 0);

  /* Set new pattern state. flash_on_ms carries the fade in+out window. */
  state.pattern = LED_PATTERN_FADE_PULSE;
  state.color = color;
  state.period_ms = period_ms > 0 ? period_ms : LED_FLASH_PERIOD_MS;
  state.flash_on_ms = fade_ms;
  state.duration_ms = 0;
  state.start_time = k_uptime_get();
  state.continuous = true;
  state.brightness = 0;
  state.fade_up = true;
  state.sequence_phase = 0;
  state.sequence_color2 = LED_COLOR_OFF;
  state.sequence_duration2 = 0;

  LOG_DBG("LED fade pulse: color=%d period=%u fade=%u", color, period_ms,
          fade_ms);

  /* Start immediately */
  k_work_schedule(&pattern_work, K_NO_WAIT);

  k_mutex_unlock(&state_mutex);
}

/* ========================================
 * High-Level Device State Indicators
 * ======================================== */

void led_indicator_show_co2_level(uint16_t co2_ppm, uint32_t duration_ms) {
  led_color_t color = led_indicator_get_co2_color(co2_ppm);
  led_indicator_show_color(color, duration_ms);
}

void led_indicator_show_low_battery(void) {
  LOG_INF("Showing low battery indicator (fading red 3s)");
  led_indicator_fade(LED_COLOR_RED, LED_FADE_DURATION_MS);
}

void led_indicator_show_charging_low_battery(void) {
  LOG_INF("Showing charging low battery (flashing red, timed)");
  /* Slow flash, but timed — the light rule says nothing stays glowing. */
  led_indicator_flash(LED_COLOR_RED, 1000, LED_STATUS_DURATION_MS);
}

void led_indicator_show_charging_normal(bool device_on, uint16_t co2_ppm) {
  LOG_INF("Showing charging normal (white 3s -> %s 5s)",
          device_on ? "CO2" : "white");

  k_mutex_lock(&state_mutex, K_FOREVER);

  /* Stop any existing pattern */
  k_work_cancel_delayable(&pattern_work);
  set_color(LED_COLOR_OFF, 0);

  /* Set up two-phase sequence */
  state.pattern = LED_PATTERN_SOLID;
  state.color = LED_COLOR_WHITE;
  state.period_ms = 0;
  state.duration_ms = LED_CHARGING_WHITE_MS;
  state.start_time = k_uptime_get();
  state.continuous = false;
  state.brightness = 100;
  state.fade_up = true;

  /* Set up phase 2 */
  state.sequence_phase = 1;
  state.sequence_color2 =
      device_on ? led_indicator_get_co2_color(co2_ppm) : LED_COLOR_WHITE;
  state.sequence_duration2 = LED_CHARGING_CO2_MS;

  /* Start immediately */
  k_work_schedule(&pattern_work, K_NO_WAIT);

  k_mutex_unlock(&state_mutex);
}

void led_indicator_show_pairing(void) {
  LOG_INF("Showing pairing mode (flashing blue)");
  led_indicator_start_flash(LED_COLOR_BLUE, LED_PAIRING_FLASH_MS);
}

void led_indicator_show_connected(uint16_t co2_ppm) {
  LOG_INF("Showing connected (white 3s -> CO2 3s)");

  k_mutex_lock(&state_mutex, K_FOREVER);

  /* Stop any existing pattern */
  k_work_cancel_delayable(&pattern_work);
  set_color(LED_COLOR_OFF, 0);

  /* Set up two-phase sequence: white then CO2 color */
  state.pattern = LED_PATTERN_SOLID;
  state.color = LED_COLOR_WHITE;
  state.period_ms = 0;
  state.duration_ms = LED_WHITE_DURATION_MS;
  state.start_time = k_uptime_get();
  state.continuous = false;
  state.brightness = 100;
  state.fade_up = true;

  /* Set up phase 2 */
  state.sequence_phase = 1;
  state.sequence_color2 = led_indicator_get_co2_color(co2_ppm);
  state.sequence_duration2 = LED_CO2_DURATION_MS;

  /* Start immediately */
  k_work_schedule(&pattern_work, K_NO_WAIT);

  k_mutex_unlock(&state_mutex);
}

void led_indicator_show_calibration(bool active) {
  if (active) {
    /* Short pulse every 5 seconds */
    led_indicator_start_flash_pulse(LED_COLOR_BLUE, 5000, 200);
  } else {
    led_indicator_stop();
  }
}

void led_indicator_show_battery_critical(void) {
  LOG_INF("Showing battery critical (fast red flash)");
  led_indicator_flash(LED_COLOR_RED, LED_BATTERY_CRITICAL_PERIOD_MS,
                      LED_BATTERY_CRITICAL_DURATION_MS);
}

void led_indicator_show_find_device(void) {
  LOG_INF("Showing find-my-device (rapid white flash)");
  led_indicator_flash(LED_COLOR_WHITE, LED_FIND_DEVICE_PERIOD_MS,
                      LED_FIND_DEVICE_DURATION_MS);
}

void led_indicator_show_status(bool battery_low, bool battery_critical,
                               bool connected) {
  /* Device-status colors, sustained then auto-off:
   * red = low battery, blue = connected to a phone, white = not connected.
   * Critical battery keeps the more urgent fast flash (still timed). Air
   * quality is NOT shown here — that is the double-press CO2 readout. */
  if (battery_critical) {
    led_indicator_show_battery_critical();
  } else if (battery_low) {
    led_indicator_show_color(LED_COLOR_RED, LED_STATUS_DURATION_MS);
  } else if (connected) {
    led_indicator_show_color(LED_COLOR_BLUE, LED_STATUS_DURATION_MS);
  } else {
    led_indicator_show_color(LED_COLOR_WHITE, LED_STATUS_DURATION_MS);
  }
}
