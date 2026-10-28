#include <unity.h>
#include "keymap_de.h"

void setUp(void) {}
void tearDown(void) {}

void test_utf8_decoding() {
    const char *s = "Aä€";
    uint32_t cp = 0;
    size_t n1 = utf8NextCodepoint(s, cp);
    TEST_ASSERT_EQUAL_UINT32(1, n1);
    TEST_ASSERT_EQUAL_UINT32('A', cp);

    size_t n2 = utf8NextCodepoint(s + n1, cp);
    TEST_ASSERT_EQUAL_UINT32(2, n2);
    TEST_ASSERT_EQUAL_UINT32(0x00E4, cp); // ä

    size_t n3 = utf8NextCodepoint(s + n1 + n2, cp);
    TEST_ASSERT_EQUAL_UINT32(3, n3);
    TEST_ASSERT_EQUAL_UINT32(0x20AC, cp); // €

    size_t n4 = utf8NextCodepoint(s + n1 + n2 + n3, cp);
    TEST_ASSERT_EQUAL_UINT32(0, n4);
}

void test_de_yz_swap() {
    HidKeyStroke k{};
    TEST_ASSERT_TRUE(mapCodepointToHid('y', KeyboardLayout::DE_QWERTZ, k));
    TEST_ASSERT_EQUAL_UINT8(0x1D, k.keycode);
    TEST_ASSERT_EQUAL_UINT8(HID_MOD_NONE, k.modifiers);

    TEST_ASSERT_TRUE(mapCodepointToHid('Z', KeyboardLayout::DE_QWERTZ, k));
    TEST_ASSERT_EQUAL_UINT8(0x1C, k.keycode);
    TEST_ASSERT_EQUAL_UINT8(HID_MOD_LSHIFT, k.modifiers);
}

void test_de_umlauts_and_altgr() {
    HidKeyStroke k{};
    // ä = 0x34, Ä = Shift+0x34
    TEST_ASSERT_TRUE(mapCodepointToHid(0x00E4, KeyboardLayout::DE_QWERTZ, k));
    TEST_ASSERT_EQUAL_UINT8(0x34, k.keycode);
    TEST_ASSERT_EQUAL_UINT8(HID_MOD_NONE, k.modifiers);

    TEST_ASSERT_TRUE(mapCodepointToHid(0x00C4, KeyboardLayout::DE_QWERTZ, k));
    TEST_ASSERT_EQUAL_UINT8(0x34, k.keycode);
    TEST_ASSERT_EQUAL_UINT8(HID_MOD_LSHIFT, k.modifiers);

    // @ = AltGr + Q (0x14)
    TEST_ASSERT_TRUE(mapCodepointToHid('@', KeyboardLayout::DE_QWERTZ, k));
    TEST_ASSERT_EQUAL_UINT8(0x14, k.keycode);
    TEST_ASSERT_EQUAL_UINT8(HID_MOD_RALT, k.modifiers);

    // \ = AltGr + ß (0x2D)
    TEST_ASSERT_TRUE(mapCodepointToHid('\\', KeyboardLayout::DE_QWERTZ, k));
    TEST_ASSERT_EQUAL_UINT8(0x2D, k.keycode);
    TEST_ASSERT_EQUAL_UINT8(HID_MOD_RALT, k.modifiers);

    // | = AltGr + < (0x64)
    TEST_ASSERT_TRUE(mapCodepointToHid('|', KeyboardLayout::DE_QWERTZ, k));
    TEST_ASSERT_EQUAL_UINT8(0x64, k.keycode);
    TEST_ASSERT_EQUAL_UINT8(HID_MOD_RALT, k.modifiers);

    // € = AltGr + E (0x08)
    TEST_ASSERT_TRUE(mapCodepointToHid(0x20AC, KeyboardLayout::DE_QWERTZ, k));
    TEST_ASSERT_EQUAL_UINT8(0x08, k.keycode);
    TEST_ASSERT_EQUAL_UINT8(HID_MOD_RALT, k.modifiers);
}

void test_de_dead_keys() {
    HidKeyStroke k{};
    TEST_ASSERT_TRUE(mapCodepointToHid('^', KeyboardLayout::DE_QWERTZ, k));
    TEST_ASSERT_EQUAL_UINT8(0x35, k.keycode);
    TEST_ASSERT_TRUE(k.deadKeySpace);

    TEST_ASSERT_TRUE(mapCodepointToHid('`', KeyboardLayout::DE_QWERTZ, k));
    TEST_ASSERT_EQUAL_UINT8(0x2E, k.keycode);
    TEST_ASSERT_EQUAL_UINT8(HID_MOD_LSHIFT, k.modifiers);
    TEST_ASSERT_TRUE(k.deadKeySpace);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_utf8_decoding);
    RUN_TEST(test_de_yz_swap);
    RUN_TEST(test_de_umlauts_and_altgr);
    RUN_TEST(test_de_dead_keys);
    return UNITY_END();
}
