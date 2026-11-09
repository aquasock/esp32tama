/*
 * pokemini_keys.c - key mapping check for the Pokémon Mini front end
 *
 * PokeMini_KeypadEvent() takes a MINX_KEY_* enum value (1..9), NOT a bit mask.
 * An earlier version of frontends/pmgui.c passed bit masks, which produced a
 * silent disaster: Z sent B, X sent A, C and Left/Right sent nothing at all,
 * and Down sent MINX_KEY_POWER — suspending the console mid-game. That read as
 * a "white screen" crash.
 *
 * This test pins the mapping. It drives each key through the public event API
 * and asserts which bit of the keypad register changed, and that releasing the
 * key puts it back. Run it after touching anything in the input path.
 *
 *   gcc -O2 -DPERFORMANCE -Icore/pokemini \
 *       tools/pokemini_keys.c core/pokemini/*.c -o pokemini_keys
 *   ./pokemini_keys
 */
#include <stdio.h>
#include "PokeMini.h"

/* Keypad register, from IOMap.h: PMR_KEY_PAD is PM_RAM[0x1052]. Bits are
 * active low on hardware; this test only cares which bit moved. */
#define KEY_PAD (PM_RAM[0x1052])

static int failures;

static void expect(const char *name, uint8_t key, uint8_t want_bit)
{
    uint8_t before = KEY_PAD;

    PokeMini_KeypadEvent(key, 1);
    uint8_t pressed_bit = (uint8_t)(before ^ KEY_PAD);

    PokeMini_KeypadEvent(key, 0);
    uint8_t after = KEY_PAD;

    int ok = (pressed_bit == want_bit) && (after == before);
    printf("%s %-6s -> bit 0x%02X (want 0x%02X)%s\n",
           ok ? "PASS" : "FAIL", name, pressed_bit, want_bit,
           after == before ? "" : "  [release did not restore the register]");
    if (!ok) failures++;
}

int main(void)
{
    if (!PokeMini_Create(0, 0)) {
        fprintf(stderr, "PokeMini_Create failed\n");
        return 2;
    }

    /* Register bit for each key, per MinxIO_Keypad(). */
    expect("up",    MINX_KEY_UP,    0x08);
    expect("down",  MINX_KEY_DOWN,  0x10);
    expect("left",  MINX_KEY_LEFT,  0x20);
    expect("right", MINX_KEY_RIGHT, 0x40);
    expect("A",     MINX_KEY_A,     0x01);
    expect("B",     MINX_KEY_B,     0x02);
    expect("C",     MINX_KEY_C,     0x04);
    expect("power", MINX_KEY_POWER, 0x80);

    /* The specific regression: Down must never touch the Power bit. This is
     * the failure that produced the white screen, so it is called out. */
    uint8_t before = KEY_PAD;
    PokeMini_KeypadEvent(MINX_KEY_DOWN, 1);
    int power_touched = (KEY_PAD ^ before) & 0x80;
    PokeMini_KeypadEvent(MINX_KEY_DOWN, 0);
    printf("%s down does not press Power\n", power_touched ? "FAIL" : "PASS");
    if (power_touched) failures++;

    printf("%s\n", failures ? "FAILED" : "OK");
    PokeMini_Destroy();
    return failures ? 1 : 0;
}
