#include <unity.h>
#include "factory_reset_state.h"

using factory_reset_state::Machine;
using factory_reset_state::Phase;
using factory_reset_state::Inputs;

void setUp(void) {}
void tearDown(void) {}

static Inputs noPress(uint32_t now) { return {now, false}; }
static Inputs press(uint32_t now)   { return {now, true}; }

static void test_starts_idle(void) {
    Machine m;
    TEST_ASSERT_EQUAL(Phase::Idle, m.phase());
    TEST_ASSERT_EQUAL_UINT32(0, m.holdMs());
}

static void test_arm_enters_awaiting_hold(void) {
    Machine m;
    m.arm(1000);
    TEST_ASSERT_EQUAL(Phase::AwaitingHold, m.phase());
}

static void test_hold_three_seconds_confirms(void) {
    Machine m;
    m.arm(1000);
    m.tick(noPress(1100));   // armed, no press yet
    m.tick(press(1500));     // press starts
    m.tick(press(2500));     // 1s of hold
    TEST_ASSERT_EQUAL(Phase::AwaitingHold, m.phase());
    m.tick(press(4500));     // 3s of hold → confirm
    TEST_ASSERT_EQUAL(Phase::Resetting, m.phase());
}

static void test_release_before_three_seconds_cancels(void) {
    Machine m;
    m.arm(1000);
    m.tick(press(1500));     // press starts
    m.tick(press(2500));     // 1s
    m.tick(noPress(3000));   // release before 3s
    TEST_ASSERT_EQUAL(Phase::Idle, m.phase());
}

static void test_window_timeout_disarms(void) {
    Machine m;
    m.arm(1000);
    m.tick(noPress(1500));   // armed, no press
    m.tick(noPress(31500));  // 30.5s — window elapsed without any hold
    TEST_ASSERT_EQUAL(Phase::Idle, m.phase());
}

static void test_release_then_repress_does_not_resume(void) {
    // Per design: any release before the 3-s threshold cancels and
    // disarms. A second press requires re-arming via the web first.
    Machine m;
    m.arm(1000);
    m.tick(press(1500));     // press starts
    m.tick(press(2500));     // 1s of hold
    m.tick(noPress(2600));   // release at 1s — cancel
    TEST_ASSERT_EQUAL(Phase::Idle, m.phase());
    m.tick(press(2700));     // re-press → still Idle, ignored
    m.tick(press(5700));     // even 3s later, still Idle
    TEST_ASSERT_EQUAL(Phase::Idle, m.phase());
}

static void test_hold_ms_reports_current_progress(void) {
    Machine m;
    m.arm(1000);
    m.tick(press(1500));     // 0 ms held at start
    TEST_ASSERT_EQUAL_UINT32(0, m.holdMs());
    m.tick(press(2500));     // 1s held
    TEST_ASSERT_EQUAL_UINT32(1000, m.holdMs());
    m.tick(noPress(2600));   // released — back to 0
    TEST_ASSERT_EQUAL_UINT32(0, m.holdMs());
}

static void test_idle_ignores_input(void) {
    Machine m;
    m.tick(press(1500));     // not armed, holding does nothing
    TEST_ASSERT_EQUAL(Phase::Idle, m.phase());
    m.tick(press(5500));
    TEST_ASSERT_EQUAL(Phase::Idle, m.phase());
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_starts_idle);
    RUN_TEST(test_arm_enters_awaiting_hold);
    RUN_TEST(test_hold_three_seconds_confirms);
    RUN_TEST(test_release_before_three_seconds_cancels);
    RUN_TEST(test_window_timeout_disarms);
    RUN_TEST(test_release_then_repress_does_not_resume);
    RUN_TEST(test_hold_ms_reports_current_progress);
    RUN_TEST(test_idle_ignores_input);
    return UNITY_END();
}
