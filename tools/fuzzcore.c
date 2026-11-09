/*
 * fuzzcore.c - state dumper for differential fuzzing
 *
 * Prints final CPU state as hex words for comparison against the Python
 * reference. Deliberately minimal: no peripherals, no display, no I/O.
 *
 *   ./fuzzcore rom.bin 400
 *   -> 5E7E 00 02 5A FA 01
 */

#include "splb32.h"
#include <stdio.h>
#include <stdlib.h>

static uint8_t *load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = (uint8_t *)malloc((size_t)n);
    if (!b) exit(1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) exit(1);
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s rom.bin steps\n", argv[0]);
        return 2;
    }
    uint8_t *rom = load(argv[1]);
    long steps = atol(argv[2]);

    splb32 *c = splb32_new(rom, SPLB32_ROM_BYTES);
    if (!c) return 1;

    for (long i = 0; i < steps; i++)
        splb32_step(c);

    splb32_state st;
    splb32_snapshot(c, &st);
    printf("%04X %02X %02X %02X %02X %02X %llu\n",
           st.pc, st.a, st.x, st.y, st.sp, st.p,
           (unsigned long long)st.cycles);

    splb32_free(c);
    free(rom);
    return 0;
}
