#pragma once

#include <stdint.h>
#include "settings_model.h"

// Returns the desired backlight duty (0..100) for the given idle duration
// and settings. Pure function; no side effects.
uint8_t backlight_compute_duty(uint32_t idle_ms,
                               const settings::Settings& s);
