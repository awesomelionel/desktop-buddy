#include "backlight.h"

uint8_t backlight_compute_duty(uint32_t idle_ms,
                               const settings::Settings& s) {
    if (s.sleep_timeout_s != 0 &&
        idle_ms >= static_cast<uint32_t>(s.sleep_timeout_s) * 1000UL) {
        return 0;
    }
    if (s.dim_timeout_s != 0 &&
        idle_ms >= static_cast<uint32_t>(s.dim_timeout_s) * 1000UL) {
        return s.dim_level_pct;
    }
    return s.full_level_pct;
}
