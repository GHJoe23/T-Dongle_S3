#include <unity.h>
#include <string.h>
#include "macro_engine.h"

void setUp(void) {}
void tearDown(void) {}

static Action emitted[16];
static int emittedCount;
static void capture(const Action &a, void *) { emitted[emittedCount++] = a; }

static MacroStep m1steps[] = {
    {0, {ActionType::Combo, "", {0x83, 0, 0, 0}, 1, 'L'}},
    {100, {ActionType::TypeText, "hi", {0, 0, 0, 0}, 0, 0}},
};
static MacroStep m2steps[] = {
    {0, {ActionType::Combo, "", {0x80, 0x82, 0, 0}, 2, 0xD4}},
};
static Macro macros[] = {{m1steps, 2}, {m2steps, 1}};

static MacroEngine *newEngine() {
    static MacroEngine e(macros, 2);
    e.begin(capture, nullptr);
    emittedCount = 0;
    return &e;
}

void test_parse_type_text() {
    MacroEngine *e = newEngine();
    TEST_ASSERT_TRUE(e->handleLine(0, "T hallo"));
    TEST_ASSERT_EQUAL(1, emittedCount);
    TEST_ASSERT_TRUE(emitted[0].type == ActionType::TypeText);
    TEST_ASSERT_EQUAL_STRING("hallo", emitted[0].text);
}

void test_parse_toggle_jiggler() {
    MacroEngine *e = newEngine();
    TEST_ASSERT_TRUE(e->handleLine(0, "J"));
    TEST_ASSERT_TRUE(emitted[0].type == ActionType::ToggleJiggler);
}

void test_parse_combo_modifiers() {
    MacroEngine *e = newEngine();
    TEST_ASSERT_TRUE(e->handleLine(0, "K STRG+ALT+ENTF"));
    TEST_ASSERT_TRUE(emitted[0].type == ActionType::Combo);
    TEST_ASSERT_EQUAL(2, emitted[0].modCount);
    TEST_ASSERT_EQUAL_UINT8(0x80, emitted[0].mods[0]);
    TEST_ASSERT_EQUAL_UINT8(0x82, emitted[0].mods[1]);
    TEST_ASSERT_EQUAL_UINT8(0xD4, emitted[0].key);
}

void test_parse_combo_gui_letter() {
    MacroEngine *e = newEngine();
    TEST_ASSERT_TRUE(e->handleLine(0, "K GUI+L"));
    TEST_ASSERT_EQUAL(1, emitted[0].modCount);
    TEST_ASSERT_EQUAL_UINT8(0x83, emitted[0].mods[0]);
    TEST_ASSERT_EQUAL_UINT8('L', emitted[0].key);
}

void test_macro_executes_steps_with_timing() {
    MacroEngine *e = newEngine();
    TEST_ASSERT_TRUE(e->handleLine(0, "M1"));
    TEST_ASSERT_TRUE(e->busy());
    e->tick(0); // Schritt 1 sofort
    TEST_ASSERT_EQUAL(1, emittedCount);
    TEST_ASSERT_TRUE(emitted[0].type == ActionType::Combo);
    e->tick(50); // Schritt 2 noch nicht fällig (100ms)
    TEST_ASSERT_EQUAL(1, emittedCount);
    e->tick(100);
    TEST_ASSERT_EQUAL(2, emittedCount);
    TEST_ASSERT_TRUE(emitted[1].type == ActionType::TypeText);
    TEST_ASSERT_EQUAL_STRING("hi", emitted[1].text);
    TEST_ASSERT_FALSE(e->busy());
}

void test_macro_busy_rejects_second() {
    MacroEngine *e = newEngine();
    TEST_ASSERT_TRUE(e->handleLine(0, "M1"));
    TEST_ASSERT_FALSE(e->handleLine(10, "M2"));
    TEST_ASSERT_TRUE(emitted[emittedCount - 1].type == ActionType::Error);
}

void test_unknown_macro_and_command() {
    MacroEngine *e = newEngine();
    TEST_ASSERT_FALSE(e->handleLine(0, "M9"));
    TEST_ASSERT_TRUE(emitted[0].type == ActionType::Error);
    emittedCount = 0;
    TEST_ASSERT_FALSE(e->handleLine(0, "X foo"));
    TEST_ASSERT_TRUE(emitted[0].type == ActionType::Error);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_type_text);
    RUN_TEST(test_parse_toggle_jiggler);
    RUN_TEST(test_parse_combo_modifiers);
    RUN_TEST(test_parse_combo_gui_letter);
    RUN_TEST(test_macro_executes_steps_with_timing);
    RUN_TEST(test_macro_busy_rejects_second);
    RUN_TEST(test_unknown_macro_and_command);
    return UNITY_END();
}
