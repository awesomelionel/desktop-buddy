#include <unity.h>
#include "backlight.h"
#include "settings_model.h"

using namespace settings;

static Settings defaults_with(uint16_t dim_s, uint8_t dim_pct,
                              uint8_t full_pct, uint16_t sleep_s) {
    Settings s = {};
    setDefaults(s, "Test");
    s.dim_timeout_s   = dim_s;
    s.dim_level_pct   = dim_pct;
    s.full_level_pct  = full_pct;
    s.sleep_timeout_s = sleep_s;
    return s;
}

void setUp(void) {}
void tearDown(void) {}

static void test_zero_idle_returns_full(void) {
    Settings s = defaults_with(30, 40, 100, 300);
    TEST_ASSERT_EQUAL_UINT8(100, backlight_compute_duty(0, s));
}

static void test_just_below_dim_threshold_returns_full(void) {
    Settings s = defaults_with(30, 40, 100, 300);
    TEST_ASSERT_EQUAL_UINT8(100, backlight_compute_duty(29'999, s));
}

static void test_at_dim_threshold_returns_dim(void) {
    Settings s = defaults_with(30, 40, 100, 300);
    TEST_ASSERT_EQUAL_UINT8(40, backlight_compute_duty(30'000, s));
}

static void test_just_below_sleep_returns_dim(void) {
    Settings s = defaults_with(30, 40, 100, 300);
    TEST_ASSERT_EQUAL_UINT8(40, backlight_compute_duty(299'999, s));
}

static void test_at_sleep_returns_off(void) {
    Settings s = defaults_with(30, 40, 100, 300);
    TEST_ASSERT_EQUAL_UINT8(0, backlight_compute_duty(300'000, s));
}

static void test_dim_disabled_skips_to_off(void) {
    Settings s = defaults_with(/*dim_s*/0, 40, 100, /*sleep_s*/60);
    TEST_ASSERT_EQUAL_UINT8(100, backlight_compute_duty(0,      s));
    TEST_ASSERT_EQUAL_UINT8(100, backlight_compute_duty(59'999, s));
    TEST_ASSERT_EQUAL_UINT8(0,   backlight_compute_duty(60'000, s));
}

static void test_sleep_disabled_caps_at_dim(void) {
    Settings s = defaults_with(/*dim_s*/30, 40, 100, /*sleep_s*/0);
    TEST_ASSERT_EQUAL_UINT8(100, backlight_compute_duty(0,        s));
    TEST_ASSERT_EQUAL_UINT8(40,  backlight_compute_duty(30'000,   s));
    TEST_ASSERT_EQUAL_UINT8(40,  backlight_compute_duty(3'600'000, s));
}

static void test_both_disabled_always_full(void) {
    Settings s = defaults_with(/*dim_s*/0, 40, 100, /*sleep_s*/0);
    TEST_ASSERT_EQUAL_UINT8(100, backlight_compute_duty(0,         s));
    TEST_ASSERT_EQUAL_UINT8(100, backlight_compute_duty(86'400'000, s));
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_zero_idle_returns_full);
    RUN_TEST(test_just_below_dim_threshold_returns_full);
    RUN_TEST(test_at_dim_threshold_returns_dim);
    RUN_TEST(test_just_below_sleep_returns_dim);
    RUN_TEST(test_at_sleep_returns_off);
    RUN_TEST(test_dim_disabled_skips_to_off);
    RUN_TEST(test_sleep_disabled_caps_at_dim);
    RUN_TEST(test_both_disabled_always_full);
    return UNITY_END();
}
