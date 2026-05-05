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

void test_dim_timeout_zero_accepted(void) {
    Settings s = make_defaults();
    s.dim_timeout_s = 0;
    char err[64] = {};
    TEST_ASSERT_TRUE(validate(s, err, sizeof(err)));
}

void test_dim_timeout_too_low_rejected(void) {
    Settings s = make_defaults();
    s.dim_timeout_s = 4;  // below DIM_TIMEOUT_MIN_S
    char err[64] = {};
    TEST_ASSERT_FALSE(validate(s, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "dim_timeout_s") != nullptr);
}

void test_dim_level_zero_rejected(void) {
    Settings s = make_defaults();
    s.dim_level_pct = 0;
    char err[64] = {};
    TEST_ASSERT_FALSE(validate(s, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "dim_level_pct") != nullptr);
}

void test_full_level_over_100_rejected(void) {
    Settings s = make_defaults();
    s.full_level_pct = 101;
    char err[64] = {};
    TEST_ASSERT_FALSE(validate(s, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "full_level_pct") != nullptr);
}

void test_dim_must_precede_sleep(void) {
    Settings s = make_defaults();
    s.dim_timeout_s   = 60;
    s.sleep_timeout_s = 60;     // both non-zero, dim NOT < sleep
    char err[64] = {};
    TEST_ASSERT_FALSE(validate(s, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "before") != nullptr);
}

void test_dim_zero_with_sleep_set_accepted(void) {
    Settings s = make_defaults();
    s.dim_timeout_s   = 0;       // dim disabled
    s.sleep_timeout_s = 60;
    char err[64] = {};
    TEST_ASSERT_TRUE(validate(s, err, sizeof(err)));
}
