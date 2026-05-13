#include <unity.h>
#include <string.h>
#include "settings_model.h"

using namespace settings;

static Settings make_defaults_cap() {
    Settings s = {};
    setDefaults(s, "Claude-AABB");
    return s;
}

void test_daily_cap_default_zero(void) {
    Settings s = make_defaults_cap();
    TEST_ASSERT_EQUAL_UINT32(0u, s.daily_token_cap);
}
