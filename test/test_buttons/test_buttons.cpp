#include <unity.h>
#include "buttons.h"

void setUp(void) {}
void tearDown(void) {}

static const uint32_t T_DEBOUNCE = 20;  // ms — must match buttons.cpp

static void test_no_event_on_high_pins(void) {
    Buttons b; buttons_init(&b);
    for (uint32_t t = 0; t < 100; t++) {
        TEST_ASSERT_EQUAL(BTN_NONE, buttons_step(&b, t, false, false, false));
    }
}

static void test_debounce_rejects_short_blip(void) {
    Buttons b; buttons_init(&b);
    // Up high for one tick at t=10, back low at t=15. Total low time 5 ms.
    buttons_step(&b, 0,  false, false, false);
    buttons_step(&b, 10, true,  false, false);
    buttons_step(&b, 15, false, false, false);
    // 50 ms later, no event has been emitted.
    for (uint32_t t = 16; t < 100; t++) {
        TEST_ASSERT_EQUAL(BTN_NONE, buttons_step(&b, t, false, false, false));
    }
}

static void test_emits_press_on_stable_low(void) {
    Buttons b; buttons_init(&b);
    buttons_step(&b, 0, false, false, false);
    buttons_step(&b, 1, true,  false, false);  // up goes pressed
    int events = 0;
    ButtonEvent last = BTN_NONE;
    for (uint32_t t = 2; t <= 1 + T_DEBOUNCE + 5; t++) {
        ButtonEvent ev = buttons_step(&b, t, true, false, false);
        if (ev != BTN_NONE) { events++; last = ev; }
    }
    TEST_ASSERT_EQUAL(1, events);
    TEST_ASSERT_EQUAL(BTN_UP, last);
}

static void test_no_repeat_while_held(void) {
    Buttons b; buttons_init(&b);
    buttons_step(&b, 0, false, false, false);
    int events = 0;
    for (uint32_t t = 1; t < 200; t++) {
        ButtonEvent ev = buttons_step(&b, t, true, false, false);
        if (ev != BTN_NONE) events++;
    }
    TEST_ASSERT_EQUAL(1, events);
}

static void test_re_arms_after_release(void) {
    Buttons b; buttons_init(&b);
    buttons_step(&b, 0, false, false, false);
    int events = 0;
    // Press, release, press — each held longer than the debounce window.
    for (uint32_t t = 1;   t < 1 + T_DEBOUNCE + 5;  t++)
        if (buttons_step(&b, t, true,  false, false) == BTN_UP) events++;
    for (uint32_t t = 50;  t < 50 + T_DEBOUNCE + 5; t++)
        buttons_step(&b, t, false, false, false);
    for (uint32_t t = 100; t < 100 + T_DEBOUNCE + 5; t++)
        if (buttons_step(&b, t, true,  false, false) == BTN_UP) events++;
    TEST_ASSERT_EQUAL(2, events);
}

static void test_priority_when_simultaneous(void) {
    Buttons b; buttons_init(&b);
    buttons_step(&b, 0, false, false, false);
    int up_events = 0, down_events = 0, center_events = 0;
    for (uint32_t t = 1; t < 1 + T_DEBOUNCE + 5; t++) {
        ButtonEvent ev = buttons_step(&b, t, true, true, true);
        if (ev == BTN_UP)     up_events++;
        if (ev == BTN_DOWN)   down_events++;
        if (ev == BTN_CENTER) center_events++;
    }
    TEST_ASSERT_EQUAL(1, center_events);
    TEST_ASSERT_EQUAL(0, down_events);
    TEST_ASSERT_EQUAL(0, up_events);
}

static void test_held_at_boot_does_not_fire(void) {
    Buttons b; buttons_init(&b);
    // First step sees up already pressed — debouncer treats that as
    // baseline, no falling edge has been observed.
    int events = 0;
    for (uint32_t t = 0; t < 200; t++) {
        ButtonEvent ev = buttons_step(&b, t, true, false, false);
        if (ev != BTN_NONE) events++;
    }
    TEST_ASSERT_EQUAL(0, events);
    // After release and re-press, we get exactly one event.
    for (uint32_t t = 200; t < 200 + T_DEBOUNCE + 5; t++)
        buttons_step(&b, t, false, false, false);
    int events2 = 0;
    for (uint32_t t = 300; t < 300 + T_DEBOUNCE + 5; t++) {
        ButtonEvent ev = buttons_step(&b, t, true, false, false);
        if (ev != BTN_NONE) events2++;
    }
    TEST_ASSERT_EQUAL(1, events2);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_no_event_on_high_pins);
    RUN_TEST(test_debounce_rejects_short_blip);
    RUN_TEST(test_emits_press_on_stable_low);
    RUN_TEST(test_no_repeat_while_held);
    RUN_TEST(test_re_arms_after_release);
    RUN_TEST(test_priority_when_simultaneous);
    RUN_TEST(test_held_at_boot_does_not_fire);
    return UNITY_END();
}
