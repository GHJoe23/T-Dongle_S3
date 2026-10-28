#include <unity.h>
#include "jiggler.h"

void setUp(void) {}
void tearDown(void) {}

void test_not_due_before_min_interval() {
    for (uint32_t seed = 1; seed <= 5; seed++) {
        Jiggler j;
        j.begin(0, seed);
        int8_t dx = 0, dy = 0;
        TEST_ASSERT_FALSE(j.poll(29999, dx, dy)); // Intervall min. 30000 ms
    }
}

void test_due_by_max_interval_and_movement_small() {
    Jiggler j;
    j.begin(0, 42);
    int8_t dx = 0, dy = 0;
    TEST_ASSERT_TRUE(j.poll(60001, dx, dy)); // Intervall max. 60000 ms
    TEST_ASSERT_INT8_WITHIN(4, 0, dx);
    TEST_ASSERT_INT8_WITHIN(4, 0, dy);
    TEST_ASSERT_FALSE(dx == 0 && dy == 0);
    TEST_ASSERT_EQUAL_UINT32(1, j.count());
}

void test_second_jiggle_inverts_first() {
    Jiggler j;
    j.begin(0, 42);
    int8_t dx1 = 0, dy1 = 0, dx2 = 0, dy2 = 0;
    TEST_ASSERT_TRUE(j.poll(60001, dx1, dy1));
    TEST_ASSERT_TRUE(j.poll(130000, dx2, dy2));
    TEST_ASSERT_EQUAL_INT8(-dx1, dx2);
    TEST_ASSERT_EQUAL_INT8(-dy1, dy2);
}

void test_instant_return_mode() {
    Jiggler j;
    j.begin(0, 42);
    j.configure(0, 10, 10, JiggleMode::INSTANT_RETURN);
    int8_t dx1 = 0, dy1 = 0, dx2 = 0, dy2 = 0;
    TEST_ASSERT_TRUE(j.poll(10000, dx1, dy1));
    TEST_ASSERT_EQUAL_UINT32(1, j.count());
    // 35 ms später erfolgt sofort die Rückbewegung
    TEST_ASSERT_FALSE(j.poll(10020, dx2, dy2));
    TEST_ASSERT_TRUE(j.poll(10035, dx2, dy2));
    TEST_ASSERT_EQUAL_INT8(-dx1, dx2);
    TEST_ASSERT_EQUAL_INT8(-dy1, dy2);
    TEST_ASSERT_EQUAL_UINT32(1, j.count());
}

void test_natural_arc_mode_zero_sum() {
    Jiggler j;
    j.begin(0, 99);
    j.configure(0, 5, 5, JiggleMode::NATURAL_ARC);
    int32_t sumX = 0, sumY = 0;
    int8_t dx = 0, dy = 0;
    uint32_t t = 5000;
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(j.poll(t, dx, dy));
        sumX += dx;
        sumY += dy;
        t += 25;
    }
    TEST_ASSERT_EQUAL_INT32(0, sumX);
    TEST_ASSERT_EQUAL_INT32(0, sumY);
    TEST_ASSERT_EQUAL_UINT32(1, j.count());
}

void test_toggle_pauses_and_resumes() {
    Jiggler j;
    j.begin(0, 7);
    int8_t dx = 0, dy = 0;
    j.toggle(1000);
    TEST_ASSERT_FALSE(j.active());
    TEST_ASSERT_FALSE(j.poll(1000000, dx, dy));
    TEST_ASSERT_EQUAL_UINT32(0, j.msUntilNext(2000));
    j.toggle(2000);
    TEST_ASSERT_TRUE(j.active());
}

void test_countdown_decreases() {
    Jiggler j;
    j.begin(0, 7);
    uint32_t c1 = j.msUntilNext(0);
    uint32_t c2 = j.msUntilNext(5000);
    TEST_ASSERT_TRUE(c1 > c2);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_not_due_before_min_interval);
    RUN_TEST(test_due_by_max_interval_and_movement_small);
    RUN_TEST(test_second_jiggle_inverts_first);
    RUN_TEST(test_instant_return_mode);
    RUN_TEST(test_natural_arc_mode_zero_sum);
    RUN_TEST(test_toggle_pauses_and_resumes);
    RUN_TEST(test_countdown_decreases);
    return UNITY_END();
}
