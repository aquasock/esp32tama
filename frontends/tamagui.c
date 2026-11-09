/*
 * tamagui.c - SDL2 desktop front end for the SPLB32 Tamagotchi core
 *
 * Runs the real V3 ROM in a window, at real time, with keyboard buttons.
 *
 *   ./tamagui                 # native, 608x624
 *   ./tamagui 2               # 2x, 1216x1248
 *   ./tamagui 3               # 3x
 *   ./tamagui 2 --headless 20 # run 20 s with no window, print timing
 *
 * Controls
 *   Z / 1 / Left    left button
 *   X / 2 / Down    middle button
 *   C / 3 / Right   right button
 *   R               reset
 *   F5              save state
 *   F9              load state
 *   --new           ignore any saved state and start a fresh egg
 *   --state PATH    use a different state file
 *   Esc / Q         quit
 *   M               mute / unmute
 *
 * State is saved automatically to $HOME/.tamagui.state on exit (and on F5), and
 * loaded on startup, so a pet survives closing the window. Pass --new to start
 * fresh instead. The ROM itself does not persist on this revision -- it reads
 * the EEPROM but never writes it during normal play -- so the emulator keeps
 * the whole machine instead.
 *
 * Buttons are active-high on PA bits 0,1,2 -- the same mapping the reference's
 * direct_input peripheral uses -- and release is reported as level -1
 * (floating), not 0.
 */

#include "splb32.h"
#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL2/SDL.h>
#include <math.h>

#ifdef EMBED_ROM
extern const unsigned char rom_embed[];
extern const unsigned int rom_embed_len;
#endif

#define MARGIN_X 16
#define MARGIN_Y 16
#define FB_W (SEG_W + MARGIN_X*2)
#define FB_H (SEG_H + MARGIN_Y*2)
#define TARGET_FPS 60

#define COL_INK   0x0000
#define COL_OFF   0xC618
#define COL_BEZEL 0x18E3

static uint16_t g_fb[FB_W * FB_H];

static void render(const uint8_t *dpram)
{
    lcd_fb fb = { g_fb, FB_W, FB_H };

    lcd_rect(&fb, 0, 0, FB_W, FB_H, COL_BEZEL);
    lcd_rect(&fb, MARGIN_X - 4, MARGIN_Y - 4,
             MARGIN_X + SEG_W + 4, MARGIN_Y + SEG_H + 4, LCD_OFF);
    lcd_draw(&fb, dpram, MARGIN_X, MARGIN_Y);
}

/* Returns a borrowed pointer for the embedded case (no copy: splb32_new only
 * stores it), or a heap buffer when loading from disk. */
static const uint8_t *load_rom(int *is_heap)
{
#ifdef EMBED_ROM
    *is_heap = 0;
    return rom_embed;
#else
    *is_heap = 1;
    FILE *f = fopen("rom.bin", "rb");
    if (!f) { perror("rom.bin"); return NULL; }
    uint8_t *r = (uint8_t *)malloc(SPLB32_ROM_BYTES);
    if (!r) { fclose(f); return NULL; }
    if (fread(r, 1, SPLB32_ROM_BYTES, f) != SPLB32_ROM_BYTES) {
        fprintf(stderr, "rom.bin is short\n");
        fclose(f);
        free(r);
        return NULL;
    }
    fclose(f);
    return r;
#endif
}

/* Map a key to a PA bit, or -1. */
static int key_mask(SDL_Keycode k)
{
    switch (k) {
    case SDLK_z: case SDLK_1: case SDLK_LEFT:  return 0x01;
    case SDLK_x: case SDLK_2: case SDLK_DOWN:  return 0x02;
    case SDLK_c: case SDLK_3: case SDLK_RIGHT: return 0x04;
    default: return -1;
    }
}

/* ---- audio -------------------------------------------------------------
 * The core yields an output level per channel. We synthesise from that: a
 * square wave whose frequency follows the timer overflow rate, with the level
 * as amplitude. The reference does not model a waveform either, so this is
 * faithful to what the emulator knows rather than to the silicon.
 * --------------------------------------------------------------------- */
#define AUD_RATE 22050

static splb32  *g_audio_cpu;
static double   g_phase0, g_phase1, g_phase0_prev, g_phase1_prev;
static int      g_muted;
static double   g_aud_step0 = 0.02, g_aud_step1 = 0.02;

static void audio_cb(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    Sint16 *out = (Sint16 *)stream;
    int n = len / (int)(2 * sizeof(Sint16));

    for (int i = 0; i < n; i++) {
        double l0 = g_audio_cpu ? splb32_audio_level(g_audio_cpu, 0) : 0.0;
        double l1 = g_audio_cpu ? splb32_audio_level(g_audio_cpu, 1) : 0.0;

        if (g_muted) { out[i * 2] = out[i * 2 + 1] = 0; continue; }

        /* advance a phase per channel; the level flips sign as a square wave */
        g_phase0 += g_aud_step0;
        g_phase1 += g_aud_step1;
        if (g_phase0 >= 1.0) g_phase0 -= 1.0;
        if (g_phase1 >= 1.0) g_phase1 -= 1.0;

        double s0 = (g_phase0 < 0.5 ? 1.0 : -1.0) * l0;
        double s1 = (g_phase1 < 0.5 ? 1.0 : -1.0) * l1;
        double v = (s0 + s1) * 0.25;            /* headroom for two channels */
        if (v > 1.0) v = 1.0;
        if (v < -1.0) v = -1.0;

        Sint16 s16 = (Sint16)(v * 32000.0);
        out[i * 2] = out[i * 2 + 1] = s16;
    }
    (void)g_phase0_prev; (void)g_phase1_prev;
}

static void audio_start(splb32 *cpu)
{
    g_audio_cpu = cpu;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "audio unavailable: %s\n", SDL_GetError());
        return;
    }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = AUD_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = audio_cb;

    if (SDL_OpenAudio(&want, &have) != 0) {
        fprintf(stderr, "audio open failed: %s\n", SDL_GetError());
        return;
    }
    SDL_PauseAudio(0);
    printf("audio: %d Hz, %d ch\n", have.freq, have.channels);
}

static void save_state(splb32 *cpu, const char *path)
{
    int want = splb32_state_size();
    uint8_t *blob = (uint8_t *)malloc((size_t)want);
    if (!blob) return;
    if (splb32_state_save(cpu, blob, want) == want) {
        FILE *f = fopen(path, "wb");
        if (f) {
            if (fwrite(blob, 1, (size_t)want, f) == (size_t)want)
                printf("saved %s\n", path);
            fclose(f);
        }
    }
    free(blob);
}

int main(int argc, char **argv)
{
    /* Unbuffered, so resume/save messages appear immediately even when stdout
     * is redirected to a file or a log. */
    setvbuf(stdout, NULL, _IONBF, 0);

    int scale = 1, headless_secs = 0, fresh_start = 0;
    const char *dump_path = NULL;
    char state_path[512];
    const char *home = getenv("HOME");
    snprintf(state_path, sizeof state_path, "%s/.tamagui.state", home ? home : ".");
    double dump_at = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--new")) {
            fresh_start = 1;
        } else if (!strcmp(argv[i], "--state") && i + 1 < argc) {
            snprintf(state_path, sizeof state_path, "%s", argv[++i]);
        } else if (!strcmp(argv[i], "--dump") && i + 2 < argc) {
            dump_at = atof(argv[++i]);
            dump_path = argv[++i];
        } else if (!strcmp(argv[i], "--headless")) {
            headless_secs = (i + 1 < argc) ? atoi(argv[++i]) : 10;
        } else {
            scale = atoi(argv[i]);
            if (scale < 1) scale = 1;
        }
    }

    int rom_is_heap = 0;
    const uint8_t *rom = load_rom(&rom_is_heap);
    if (!rom) return 1;

    splb32 *cpu = splb32_new(rom, SPLB32_ROM_BYTES);
    if (!cpu) { fprintf(stderr, "core init failed\n"); return 1; }

    /* Same config as the reference .brick: PD bit 1 pulled up. */
    splb32_set_pullup(cpu, 3, 0x02);
    splb32_attach_eeprom(cpu, 0xA0, 2, 1);
    splb32_attach_ir(cpu, 0x04, 0x04);   /* PB bit2 receive, PC bit2 transmit */

    if (!fresh_start) {
        FILE *sf = fopen(state_path, "rb");
        if (sf) {
            int want = splb32_state_size();
            uint8_t *blob = (uint8_t *)malloc((size_t)want);
            size_t got = blob ? fread(blob, 1, (size_t)want, sf) : 0;
            fclose(sf);
            int rc = (got == (size_t)want) ? splb32_state_load(cpu, blob, want) : -1;
            free(blob);
            if (rc == 0) printf("resumed from %s\n", state_path);
            else printf("could not load %s (rc=%d), starting fresh\n", state_path, rc);
        } else {
            printf("no saved state at %s, starting fresh\n", state_path);
        }
    }

    splb32_state st;
    splb32_snapshot(cpu, &st);

    /* ---- dump mode: run to a point in emulated time, write one PPM ---- */
    if (dump_path) {
        splb32_run_cycles(cpu, (uint64_t)(dump_at * (double)SPLB32_CLOCK_HZ));
        splb32_snapshot(cpu, &st);
        render(st.dpram);

        FILE *f = fopen(dump_path, "wb");
        if (!f) { perror(dump_path); return 1; }
        fprintf(f, "P6\n%d %d\n255\n", FB_W, FB_H);
        for (int i = 0; i < FB_W * FB_H; i++) {
            uint16_t p = g_fb[i];
            unsigned char px[3] = {
                (unsigned char)(((p >> 11) & 31) * 255 / 31),
                (unsigned char)(((p >> 5) & 63) * 255 / 63),
                (unsigned char)((p & 31) * 255 / 31)
            };
            fwrite(px, 1, 3, f);
        }
        fclose(f);

        int lit = 0;
        for (int i = 0; i < SEG_COUNT; i++) {
            const seg_entry *sg = &seg_map[i];
            if ((st.dpram[sg->byte] >> sg->bit) & 1) lit++;
        }
        printf("dumped %s at %.2f s emulated, %d/%d segments lit\n",
               dump_path, dump_at, lit, SEG_COUNT);
        splb32_free(cpu);
        if (rom_is_heap) free((void *)rom);
        return 0;
    }

    /* ---- headless mode: no window, real-time pacing, no SDL video ---- */
    if (headless_secs) {
        const uint64_t per_sec = SPLB32_CLOCK_HZ;
        const uint64_t total = per_sec * (uint64_t)headless_secs;
        const uint64_t c0 = st.cycles;
        struct timespec t0, now;
        clock_gettime(CLOCK_MONOTONIC, &t0);

        while (st.cycles - c0 < total) {
            splb32_run_cycles(cpu, 200000);
            splb32_snapshot(cpu, &st);

            clock_gettime(CLOCK_MONOTONIC, &now);
            double elapsed = (double)(now.tv_sec - t0.tv_sec)
                           + (double)(now.tv_nsec - t0.tv_nsec) / 1e9;
            uint64_t target = (uint64_t)(elapsed * (double)per_sec);
            uint64_t done = st.cycles - c0;
            if (target > done + 200000) {
                struct timespec req = {0, 1000000};   /* 1 ms */
                nanosleep(&req, NULL);
            }
        }

        double secs = (double)(st.cycles - c0) / (double)per_sec;
        printf("headless, %.2f s emulated\n", secs);
        printf("  instructions      %llu\n", (unsigned long long)st.instrs);
        printf("  final pc          %04X\n", st.pc);
        printf("  dpram[0..7]       ");
        for (int i = 0; i < 8; i++) printf("%02X ", st.dpram[i]);
        printf("\n");
        splb32_free(cpu);
        if (rom_is_heap) free((void *)rom);
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow(
        "Tamagotchi Connection V3 - SPLB32 core",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        FB_W * scale, FB_H * scale, SDL_WINDOW_SHOWN);
    if (!win) { fprintf(stderr, "CreateWindow: %s\n", SDL_GetError()); return 1; }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "CreateRenderer: %s\n", SDL_GetError()); return 1; }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");   /* nearest */

    audio_start(cpu);

    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
                                         SDL_TEXTUREACCESS_STREAMING, FB_W, FB_H);
    if (!tex) { fprintf(stderr, "CreateTexture: %s\n", SDL_GetError()); return 1; }

    /* Boot for a moment so the first frame is not empty. */
    splb32_run_cycles(cpu, SPLB32_CLOCK_HZ / 2);

    const Uint64 freq = SDL_GetPerformanceFrequency();
    const Uint64 t_start = SDL_GetPerformanceCounter();
    const uint64_t cycles_per_sec = SPLB32_CLOCK_HZ;

    /* Pacing is relative: `base_cycles` is the emulated clock at t_start, and
     * `emu` must be re-based whenever the machine is reset or a state is
     * loaded. Using the absolute cycle count against a relative wall clock is
     * what made a resumed pet move once and then freeze -- target was always
     * far below emu, so the loop never ran the CPU again. */
    uint64_t base_cycles = st.cycles;
    uint64_t emu = st.cycles;
    uint64_t frames = 0;
    int running = 1;
    int btn[3] = {0, 0, 0};      /* current PA bit state per button */
    const uint8_t btnbit[3] = {0x01, 0x02, 0x04};

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = 0;
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_q) running = 0;
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_m) {
                g_muted = !g_muted;
                printf("audio %s\n", g_muted ? "muted" : "on");
            }
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F5) {
                save_state(cpu, state_path);
            }
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F9) {
                FILE *sf = fopen(state_path, "rb");
                if (sf) {
                    int want = splb32_state_size();
                    uint8_t *blob = (uint8_t *)malloc((size_t)want);
                    size_t got = blob ? fread(blob, 1, (size_t)want, sf) : 0;
                    fclose(sf);
                    if (got == (size_t)want && splb32_state_load(cpu, blob, want) == 0) {
                        printf("loaded %s\n", state_path);
                        splb32_snapshot(cpu, &st);
                        base_cycles = st.cycles;   /* re-base, else it freezes */
                        emu = st.cycles;
                    }
                    free(blob);
                }
            }
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_r) {
                splb32_reset(cpu);
                splb32_set_pullup(cpu, 3, 0x02);
                splb32_snapshot(cpu, &st);
                base_cycles = st.cycles;      /* re-base the pacing clock */
                emu = st.cycles;
            }
            else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                int m = key_mask(e.key.keysym.sym);
                if (m >= 0) {
                    int idx = (m == 0x01) ? 0 : (m == 0x02) ? 1 : 2;
                    int down = (e.type == SDL_KEYDOWN);
                    if (e.key.repeat) down = btn[idx];
                    if (down != btn[idx]) {
                        btn[idx] = down;
                        splb32_pin(cpu, 0, btnbit[idx], down ? 1 : -1);
                    }
                }
            }
        }

        /* Pace to wall clock. */
        double elapsed = (double)(SDL_GetPerformanceCounter() - t_start) / (double)freq;
        uint64_t target = base_cycles + (uint64_t)(elapsed * (double)cycles_per_sec);
        if (target > emu) {
            uint64_t chunk = target - emu;
            if (chunk > cycles_per_sec / 10) chunk = cycles_per_sec / 10;  /* cap catch-up */
            splb32_run_cycles(cpu, chunk);
            emu += chunk;
        } else if (emu > target + cycles_per_sec / 4) {
            /* running ahead (e.g. after reset); let the clock catch up */
            SDL_Delay(1);
        }

        splb32_snapshot(cpu, &st);
        render(st.dpram);
        SDL_UpdateTexture(tex, NULL, g_fb, FB_W * sizeof(uint16_t));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        frames++;

        /* Keep the frame rate sane; the pacing above does the real work. */
        SDL_Delay(1);
    }

    save_state(cpu, state_path);

    printf("frames %llu, emulated cycles %llu (~%.1f s)\n",
           (unsigned long long)frames,
           (unsigned long long)(st.cycles),
           (double)st.cycles / 4000000.0);

    SDL_CloseAudio();
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    splb32_free(cpu);
    if (rom_is_heap) free((void *)rom);
    return 0;
}
