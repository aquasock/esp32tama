/*
 * corebench.c - timing and validation driver for the SPLB32 C core
 *
 *   ./corebench rom.bin                 # benchmark, prints timings
 *   ./corebench rom.bin --trace 200 5000 # dump state every 5000 instrs, 200 lines
 *   ./corebench rom.bin --final          # dump final state for comparison
 *
 * --trace output is designed to be diffed against the Python reference
 * trace to prove the two cores execute identically.
 */

#include "splb32.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

#ifdef EMBED_ROM
extern const unsigned char rom_embed[];
extern const unsigned int rom_embed_len;
#endif

static uint8_t *load_rom(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t *)malloc((size_t)n + SPLB32_ROM_BYTES);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

int main(int argc, char **argv)
{
#ifndef EMBED_ROM
    if (argc < 2) {
        fprintf(stderr, "usage: %s rom.bin [--trace N STEP] [--final] [--cycles N] [--instrs N]\n", argv[0]);
        return 2;
    }
#endif

    /* splb32_new only stores this pointer; it never writes to the ROM. So an
     * embedded ROM needs no copy at all, which saves 512 KB of heap on targets
     * where a contiguous block that size is hard to find (NOMMU in
     * particular). */
    size_t romlen = 0;
    const uint8_t *rom;
    int rom_is_heap = 0;
#ifdef EMBED_ROM
    (void)load_rom;
    (void)argv;
    rom = rom_embed;
    romlen = rom_embed_len;
#else
    rom = load_rom(argv[1], &romlen);
    if (!rom) return 1;
    rom_is_heap = 1;
#endif

    int do_trace = 0, do_final = 0, use_eeprom = 0;
    long trace_lines = 0, trace_step = 1000;
    uint64_t cycles = 40000000ull;
    uint64_t instrs = 0;

#ifdef EMBED_ROM
    int argbase = 1;
#else
    int argbase = 2;
#endif
    for (int i = argbase; i < argc; i++) {
        if (!strcmp(argv[i], "--trace") && i + 2 < argc) {
            do_trace = 1;
            trace_lines = atol(argv[++i]);
            trace_step = atol(argv[++i]);
        } else if (!strcmp(argv[i], "--eeprom")) {
            use_eeprom = 1;
        } else if (!strcmp(argv[i], "--final")) {
            do_final = 1;
        } else if (!strcmp(argv[i], "--cycles") && i + 1 < argc) {
            cycles = strtoull(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "--instrs") && i + 1 < argc) {
            instrs = strtoull(argv[++i], NULL, 0);
        }
    }

    splb32 *cpu = splb32_new(rom, romlen);
    if (!cpu) { fprintf(stderr, "core init failed (rom must be >= %u bytes)\n", SPLB32_ROM_BYTES); return 1; }
    splb32_set_pullup(cpu, 3, 0x02);
    if (use_eeprom) splb32_attach_eeprom(cpu, 0xA0, 2, 1);

    if (do_trace) {
        splb32_state st;
        for (long i = 0; i < trace_lines; i++) {
            splb32_run_instrs(cpu, (uint64_t)trace_step);
            splb32_snapshot(cpu, &st);
            printf("%llu %llu %04X %02X %02X %02X %02X %02X\n",
                   (unsigned long long)st.instrs, (unsigned long long)st.cycles,
                   st.pc, st.a, st.x, st.y, st.sp, st.p);
        }
        splb32_free(cpu);
        if (rom_is_heap) free((void *)rom);
        return 0;
    }

    if (do_final) {
        if (instrs) splb32_run_instrs(cpu, instrs);
        else        splb32_run_cycles(cpu, cycles);
        splb32_state st;
        splb32_snapshot(cpu, &st);
        printf("instrs %llu\n", (unsigned long long)st.instrs);
        printf("cycles %llu\n", (unsigned long long)st.cycles);
        printf("pc %04X\n", st.pc);
        printf("regs %02X %02X %02X %02X\n", st.a, st.x, st.y, st.sp);
        printf("ps %02X\n", st.p);
        unsigned long h = 2166136261u;
        for (size_t i = 0; i < sizeof(st.ram); i++) { h ^= st.ram[i]; h *= 16777619u; }
        printf("ram_fnv %08lX\n", h & 0xFFFFFFFFul);
        h = 2166136261u;
        for (size_t i = 0; i < sizeof(st.dpram); i++) { h ^= st.dpram[i]; h *= 16777619u; }
        printf("dpram_fnv %08lX\n", h & 0xFFFFFFFFul);
        if (use_eeprom) {
            uint8_t img[1024];
            splb32_eeprom_save(cpu, img);
            h = 2166136261u;
            for (size_t i = 0; i < sizeof(img); i++) { h ^= img[i]; h *= 16777619u; }
            printf("eeprom_fnv %08lX\n", h & 0xFFFFFFFFul);
        }
        splb32_free(cpu);
        if (rom_is_heap) free((void *)rom);
        return 0;
    }

    /* benchmark */
    splb32_state st;
    splb32_run_instrs(cpu, 200000);          /* warm up */
    splb32_snapshot(cpu, &st);

    uint64_t c0 = st.cycles, i0 = st.instrs;
    uint64_t t0 = now_ns();
    splb32_run_cycles(cpu, 40000000ull);     /* 10 emulated seconds */
    uint64_t ns = now_ns() - t0;
    splb32_snapshot(cpu, &st);
    uint64_t dc = st.cycles - c0, di = st.instrs - i0;

    double secs = (double)ns / 1e9;
    printf("SPLB32 C core benchmark\n");
    printf("  emulated cycles   %llu\n", (unsigned long long)dc);
    printf("  instructions      %llu\n", (unsigned long long)di);
    printf("  avg cycles/instr  %.2f\n", (double)dc / (double)di);
    printf("  wall time         %.3f s\n", secs);
    printf("  emulated cycles/s %.0f\n", (double)dc / secs);
    printf("  instructions/s    %.0f\n", (double)di / secs);
    printf("  realtime factor   %.2fx   (target 4,000,000 cycles/s)\n",
           ((double)dc / secs) / 4000000.0);

    splb32_free(cpu);
    if (rom_is_heap) free((void *)rom);
    return 0;
}
