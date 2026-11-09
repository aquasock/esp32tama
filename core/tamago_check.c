/*
 * tamago_check.c - peripheral-level checks for the Tama-Go firmware
 *
 * Complements conformance.c, which validates the CPU alone. This one loads a
 * real Tama-Go ROM and asserts the display model against the firmware's own
 * blit routine, so a regression in the LCD geometry or the framebuffer handoff
 * shows up as a failure rather than as a slightly wrong picture.
 *
 *   gcc -O2 -o tamago_check core/tamago_check.c core/gplb.c
 *   ./tamago_check /path/to/tamago.rom
 *
 * Checks:
 *   1. The 31-row blit at $F050 copies $0400+12*i to the table destination
 *      $F13C[2i]<<8 | $F13C[2i+1], byte for byte. The pointers are stored high
 *      byte first.
 *   2. The destination addresses cover $100C-$117F exactly once, with no
 *      overlap between the ascending and descending halves.
 *   3. TIMER1 reloads and expires at a rate consistent with a 64-common frame.
 *
 * Exit status: 0 all checks passed, 1 a check failed.
 */
#include "gplb.h"
#include <stdio.h>
#include <stdlib.h>

#define FIXED_BLOCK   0x00000u     /* ROM image offset of the $C000-$FFFF bank */
#define TBL_DST       0xF13Cu
#define FRAMEBUFFER   0x0400u
#define ROWS          31
#define ROW_BYTES     12

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <rom>\n", argv[0]); return 2; }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *rom = malloc((size_t)n);
    if (!rom || fread(rom, 1, (size_t)n, f) != (size_t)n) { fclose(f); return 2; }
    fclose(f);

    gplb *c = gplb_new(rom, (size_t)n);
    if (!c) { fprintf(stderr, "rom too small\n"); return 2; }

    /* Run far enough for the boot path to complete the initial blit. */
    gplb_run_instrs(c, 4000000);

    gplb_state s;
    gplb_snapshot(c, &s);

    /* --- 1. blit reproduces the framebuffer exactly --- */
    int mismatches = 0;
    for (int i = 0; i < ROWS; i++) {
        uint32_t src = FRAMEBUFFER + ROW_BYTES * (uint32_t)i;
        const uint8_t *t = rom + FIXED_BLOCK + (TBL_DST - 0xC000u) + 2 * i;
        uint32_t dst = ((uint32_t)t[0] << 8) | t[1];
        for (int k = 0; k < ROW_BYTES; k++) {
            uint8_t a = s.ram[src + (uint32_t)k];
            uint8_t b = s.dpram[dst - GPLB_DPRAM_BASE + (uint32_t)k];
            if (a != b) mismatches++;
        }
    }
    check(mismatches == 0, "blit: LCD RAM matches the $0400 framebuffer");

    /* --- 2. destination coverage --- */
    static int seen[GPLB_DPRAM_SIZE];
    int covered = 0, doubled = 0, oob = 0;
    for (int i = 0; i < ROWS; i++) {
        const uint8_t *t = rom + FIXED_BLOCK + (TBL_DST - 0xC000u) + 2 * i;
        uint32_t dst = (((uint32_t)t[0] << 8) | t[1]) - GPLB_DPRAM_BASE;
        for (int k = 0; k < ROW_BYTES; k++) {
            if (dst + (uint32_t)k >= GPLB_DPRAM_SIZE) { oob++; continue; }
            if (seen[dst + (uint32_t)k]) doubled++;
            else { seen[dst + (uint32_t)k] = 1; covered++; }
        }
    }
    check(oob == 0,        "geometry: all blit destinations are inside LCD RAM");
    check(doubled == 0,    "geometry: no destination byte is written twice");
    check(covered == ROWS * ROW_BYTES, "geometry: 31 rows x 12 bytes covered");

    /* --- 3. tick rate --- */
    gplb_state a, b;
    gplb_snapshot(c, &a);
    gplb_run_instrs(c, 10000000);
    gplb_snapshot(c, &b);
    double secs = (double)(b.cycles - a.cycles) / (double)GPLB_CLOCK_HZ;
    double hz   = secs > 0 ? (double)(b.nmi - a.nmi) / secs : 0.0;
    printf("     TIMER1 tick: %.1f Hz (%llu expiries)\n", hz,
           (unsigned long long)(b.nmi - a.nmi));
    check(hz > 50.0 && hz < 75.0, "timer: tick is in the 50-75 Hz frame band");

    gplb_free(c);
    free(rom);
    printf("%s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
