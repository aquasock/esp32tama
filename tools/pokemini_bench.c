/*
 * pokemini_bench.c - headless harness for the vendored PokeMini core
 *
 * Runs a cartridge image for a fixed number of emulated frames and reports the
 * numbers that decide whether this platform can ship on the target hardware:
 * realtime factor, and instructions needed per frame against the frame budget.
 *
 *   gcc -O2 -DPERFORMANCE -Icore/pokemini \
 *       tools/pokemini_bench.c core/pokemini/*.c -o pokemini_bench
 *   ./pokemini_bench [rom.min] [frames] [--quiet] [--synthetic]
 *
 * With no ROM argument, or with --synthetic, a deterministic pseudo-random
 * cartridge is generated in memory. The interpreter does not care what the
 * opcodes mean, so this exercises the same dispatch, memory and interrupt paths
 * as a real title without needing any ROM file - which matters because no
 * commercial ROM may be distributed and the target device has no ROM on it.
 *
 * Frames default to 4000. Shorter runs measure the CPU frequency ramp rather
 * than the core: the same ROM and binary reports 107 M instructions/sec at 200
 * frames and 194 M at 20000, so anything under a second of wall time is noise.
 *
 * The Pokemon Mini runs at 4 MHz with a four-cycle instruction fetch, so its
 * requirement is 1,000,000 instructions per second. At 72 Hz that is 13,889
 * instructions per frame; anything the core needs above that has to be found
 * elsewhere, and anything below it is headroom.
 *
 * Frames are emulated as fixed 55,555-cycle slices (4 MHz / 72 Hz) rather than
 * through PokeMini_EmulateFrame(), which waits on a vertical blank that the free
 * BIOS never produces. Slicing by cycles is also what a target port wants, since
 * there is no vsync to wait for either.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "PokeMini.h"

extern int PokeMini_EmulateCycles(int);
extern unsigned long g_exec_count;   /* added by tools/pokemini_bench.sh */

#define PM_CLOCK_HZ      4000000
#define PM_FRAME_HZ      72
#define CYCLES_PER_FRAME (PM_CLOCK_HZ / PM_FRAME_HZ)   /* 55555 */
#define BUDGET_PER_FRAME 13889                          /* 1 MIPS / 72 Hz */

/* Deterministic xorshift32, so a synthetic run is reproducible across hosts. */
static uint32_t g_rng = 0x12345678u;
static uint32_t rnd(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

/* Build a synthetic cartridge. A real .min word is 12 bits; randomising them
 * gives a realistic spread of instruction types and operand patterns, and the
 * interpreter treats every one the same way it would a real title's. */
/* 32 KB is the largest a PM cartridge gets, so this is the worst case for the
 * ROM read path. A static buffer rather than malloc, so a target build has no
 * heap dependency at all and cannot fail on a fragmented allocator - this board
 * is NOMMU and has been bitten by that before. */
#define SYNTH_BYTES 32768
static unsigned char g_synth[SYNTH_BYTES];

static unsigned char *make_synthetic(unsigned bytes)
{
    if (bytes > SYNTH_BYTES) bytes = SYNTH_BYTES;
    for (unsigned i = 0; i < bytes; i++)
        g_synth[i] = (unsigned char)(rnd() & 0xFF);
    return g_synth;
}

static int bench_rom(const char *path, int frames, int quiet);

static double now_sec(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static int bench_rom(const char *path, int frames, int quiet)
{
    /* path == NULL means synthetic: no ROM file, no copyright content. */
    long len = 0;
    unsigned char *rom = NULL;

    if (path) {
        FILE *f = fopen(path, "rb");
        if (!f) { perror(path); return 2; }
        fseek(f, 0, SEEK_END);
        len = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (len <= 0) { fprintf(stderr, "%s: empty\n", path); fclose(f); return 2; }
        rom = malloc((size_t)len);
        if (!rom || fread(rom, 1, (size_t)len, f) != (size_t)len) {
            fprintf(stderr, "%s: read failed\n", path); fclose(f); return 2;
        }
        fclose(f);
    } else {
        /* 32 KB of synthetic cartridge, which is the largest a PM cartridge gets
         * and therefore the worst case for the ROM read path. */
        len = 32768;
        rom = make_synthetic((unsigned)len);
        if (!rom) return 2;
    }

    if (!PokeMini_Create(0, 0)) { fprintf(stderr, "PokeMini_Create failed\n"); return 1; }
    if (!PokeMini_SetMINMem(rom, (int)len)) {
        fprintf(stderr, "%s: not a valid cartridge image\n", path);
        return 1;
    }
    /* SetMINMem only points the core at the image. Without a reset the CPU is
     * still wherever PokeMini_Create left it and the BIOS never runs - which
     * looks exactly like a working emulator measuring 6,945 instructions per
     * frame, because that is the BIOS idle loop. The front ends call
     * PokeMini_Reset() after loading for this reason. */
    PokeMini_Reset(0);

    /* Let the BIOS hand over to the cartridge before measuring, so the reset
     * sequence is not counted against the per-frame cost. */
    PokeMini_EmulateCycles(CYCLES_PER_FRAME * 8);

    unsigned long steps0 = g_exec_count;
    double t0 = now_sec();
    /* EmulateCycles returns the *remaining* count, which goes negative when the
     * last instruction overshoots the slice, so the sum is not a total. Ignore
     * the return value; the loop is what advances time. */
    for (int i = 0; i < frames; i++) PokeMini_EmulateCycles(CYCLES_PER_FRAME);
    double wall = now_sec() - t0;
    unsigned long steps = g_exec_count - steps0;

    double fps_now   = wall > 0 ? frames / wall : 0.0;
    double realtime  = fps_now / PM_FRAME_HZ;
    double steps_sec = wall > 0 ? steps / wall : 0.0;
    double per_frame = (double)steps / frames;

    if (!quiet) {
        printf("rom       %s (%ld bytes)\n", path ? path : "synthetic", len);
        printf("frames    %d  (%d cycles each, %.1f emulated seconds)\n",
               frames, CYCLES_PER_FRAME, (double)frames * CYCLES_PER_FRAME / PM_CLOCK_HZ);
        printf("wall      %.3f s\n", wall);
        printf("core      %.0f instructions/sec\n", steps_sec);
        printf("realtime  %.2fx  (1.00x = 72 fps on this host)\n", realtime);
        printf("per frame %.0f instructions, budget %d  (%.0f%% used)\n",
               per_frame, BUDGET_PER_FRAME, 100.0 * per_frame / BUDGET_PER_FRAME);
    } else {
        printf("%.2f %.0f\n", realtime, per_frame);
    }

    PokeMini_Destroy();
    if (rom != g_synth) free(rom);
    return 0;
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    int frames = 4000;
    int quiet = 0, synthetic = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--quiet")) { quiet = 1; continue; }
        if (!strcmp(argv[i], "--synthetic")) { synthetic = 1; continue; }
        if (argv[i][0] == '-') { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }

        /* A bare number is the frame count; anything else is the ROM path.
         * Order-independent, so "--synthetic 2000" and "2000 --synthetic" both
         * mean the same thing. */
        int all_digits = 1;
        for (const char *c = argv[i]; *c; c++) if (*c < '0' || *c > '9') all_digits = 0;
        if (all_digits && argv[i][0]) frames = atoi(argv[i]);
        else if (!path) path = argv[i];
        else { fprintf(stderr, "unexpected argument %s\n", argv[i]); return 2; }
    }
    if (synthetic) path = NULL;

    if (!path && !synthetic) {
        fprintf(stderr,
            "usage: %s [rom.min] [frames] [--quiet] [--synthetic]\n"
            "  with no ROM, or with --synthetic, a pseudo-random cartridge is\n"
            "  generated in memory so the benchmark needs no ROM file.\n", argv[0]);
    }
    return bench_rom(path, frames, quiet);
}
