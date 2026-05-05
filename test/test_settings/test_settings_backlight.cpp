#include <unity.h>
#include <string.h>
#include "settings_model.h"

using namespace settings;

static Settings make_defaults() {
    Settings s = {};
    setDefaults(s, "Claude-AABB");
    return s;
}

void test_backlight_defaults(void) {
    Settings s = make_defaults();
    TEST_ASSERT_EQUAL_UINT16(30, s.dim_timeout_s);
    TEST_ASSERT_EQUAL_UINT8(40, s.dim_level_pct);
    TEST_ASSERT_EQUAL_UINT8(100, s.full_level_pct);
}
