/*
 * tamap1gui.c - SDL2 desktop front end for the original Tamagotchi (P1/P2)
 *
 * Runs TamaLib (vendored, GPL-2.0-or-later, taken here under GPL-3.0) against a
 * 32x16 dot-matrix LCD with 8 status icons, which is the E0C6S46's display.
 *
 *   ./tamap1gui <rom.bin> [--scale N]
 *
 * <rom.bin> is a 12,288-byte first-gen Tamagotchi ROM. Each 12-bit program word
 * is stored as two bytes with the first byte's high nibble unused, so the file
 * holds 6,144 words. That is the convention TamaTool uses and it is what real
 * dumps look like.
 *
 * Keys
 *   A / left arrow    left button
 *   S / down arrow    middle button
 *   D / right arrow   right button
 *   space             tap sensor (shake)
 *   F1 / F2           scale down / up
 *   F5 / F9           save / load state (not yet implemented, see docs)
 *   Esc               quit
 *
 * No ROM is distributed with this. The operator supplies their own dump.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include <SDL2/SDL.h>

#include "tamalib.h"

#define DEFAULT_SCALE   4
#define LCD_W           LCD_WIDTH     /* 32 */
#define LCD_H           LCD_HEIGHT    /* 16 */
#define ICON_N            ICON_NUM    /* 8 */
#define ICON_CELL       5             /* icon cell size in panel pixels */
#define GAP              2             /* bezel around the panel, panel pixels */

/* Panel geometry is per-device at run time: 32 dots for the Tamagotchi family,
 * 39 for Alien Fever. LCD_WIDTH is the widest supported, so the buffers below
 * are sized for the maximum and the active width comes from hw_get_pixel_width(). */
#define PANEL_W_MAX (LCD_W * ICON_CELL)
#define PANEL_H     (LCD_H * ICON_CELL + ICON_CELL * 2)
#define TEX_W_MAX   (PANEL_W_MAX + GAP * 2)
#define TEX_H       (PANEL_H + GAP * 2)

static int g_panel_w;      /* active panel width in panel pixels */
static int g_tex_w, g_tex_h;

#define COL_ON     0xFF1A1A1Eu
#define COL_OFF    0xFFC9C9BEu
#define COL_BEZEL  0xFF303036u
#define COL_ICON_BG 0xFFE8E8DEu

/* ---- state the HAL delivers into ------------------------------------- */

static bool_t g_matrix[LCD_W][LCD_H];
static bool_t g_icons[ICON_N];

/* ---- ROM loading ------------------------------------------------------ */

static u12_t *load_rom(const char *path, u32_t *count)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || (n % 2) != 0) {
        fprintf(stderr, "%s: size %ld is not a whole number of 12-bit words\n", path, n);
        fclose(f);
        return NULL;
    }

    u32_t words = (u32_t)n / 2;
    u12_t *prog = malloc(words * sizeof(u12_t));
    if (!prog) { fclose(f); return NULL; }

    for (u32_t i = 0; i < words; i++) {
        int b0 = fgetc(f), b1 = fgetc(f);
        if (b0 == EOF || b1 == EOF) { free(prog); fclose(f); return NULL; }
        /* High nibble of the first byte is unused padding; the word is the low
         * nibble of byte 0 followed by byte 1. */
        prog[i] = (u12_t)(b1 | ((b0 & 0x0F) << 8));
    }
    fclose(f);
    *count = words;
    return prog;
}

/* ---- HAL -------------------------------------------------------------- */

static timestamp_t hal_get_timestamp(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (timestamp_t)((u32_t)t.tv_sec * 1000000u + (u32_t)(t.tv_nsec / 1000));
}

static void hal_sleep_until(timestamp_t ts)
{
    /* Signed difference so wrap-around is handled: a deadline that has already
     * passed gives a negative remaining time rather than a huge one. */
    int32_t remaining = (int32_t)(ts - hal_get_timestamp());
    if (remaining <= 0) return;

    struct timespec t;
    t.tv_sec  = remaining / 1000000;
    t.tv_nsec = (remaining % 1000000) * 1000L;
    nanosleep(&t, NULL);
}

static void hal_set_lcd_matrix(u8_t x, u8_t y, bool_t val)
{
    if (x < LCD_W && y < LCD_H) g_matrix[x][y] = val;
}

static void hal_set_lcd_icon(u8_t icon, bool_t val)
{
    if (icon < ICON_N) g_icons[icon] = val;
}

/* Buzzer. TamaLib reports a frequency in deci-Hz; we synthesise a square wave
 * from it at the mixer callback. */
/* Deci-Hz. Initialised to the hardware reset value, which is frequency code 0 =
 * 4096 Hz (see hw_set_buzzer_freq case 0). Leaving this at 0 meant "silent", and
 * that silenced every device which enables the buzzer without ever writing the
 * frequency register - Digimon does exactly that: it toggles the enable port at
 * 0xF54 and never touches 0xF74. On real hardware an unset frequency register
 * means the reset default, not silence. */
static u32_t g_buzz_freq_dhz = 40960;
static volatile int g_buzz_on;
static double g_phase;

static void hal_set_frequency(u32_t freq) { g_buzz_freq_dhz = freq; }
static void hal_play_frequency(bool_t en) { g_buzz_on = en ? 1 : 0; }

static void audio_cb(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    Sint16 *out = (Sint16 *)stream;
    int frames = len / (int)(2 * sizeof(Sint16));

    if (!g_buzz_on || g_buzz_freq_dhz == 0) {
        memset(out, 0, (size_t)len);
        g_phase = 0.0;
        return;
    }

    /* deci-Hz -> Hz, and it can be high; clamp to something audible. */
    double hz = g_buzz_freq_dhz / 10.0;
    if (hz < 20.0) hz = 20.0;
    if (hz > 8000.0) hz = 8000.0;
    double step = hz / 44100.0;

    for (int i = 0; i < frames; i++) {
        g_phase += step;
        if (g_phase >= 1.0) g_phase -= 1.0;
        Sint16 s = (g_phase < 0.5) ? 4000 : -4000;
        out[i * 2] = out[i * 2 + 1] = s;
    }
}

static int g_quit;

static int hal_handler(void) { return g_quit; }

static bool_t hal_is_log_enabled(log_level_t level)
{
    /* Only errors. TamaLib is chatty at LOG_CPU and it would flood the console. */
    return (level == LOG_ERROR);
}

static void hal_log(log_level_t level, char *buff, ...)
{
    if (!hal_is_log_enabled(level)) return;
    va_list ap;
    va_start(ap, buff);
    fputs("tamalib: ", stderr);
    vfprintf(stderr, buff, ap);
    va_end(ap);
}

static void hal_halt(void) { }

static void *hal_malloc(u32_t size) { return malloc(size); }
static void  hal_free(void *p) { free(p); }

static hal_t g_hal_impl = {
    .malloc             = hal_malloc,
    .free               = hal_free,
    .halt               = hal_halt,
    .is_log_enabled     = hal_is_log_enabled,
    .log                = hal_log,
    .sleep_until        = hal_sleep_until,
    .get_timestamp      = hal_get_timestamp,
    .update_screen      = NULL,          /* set below */
    .set_lcd_matrix     = hal_set_lcd_matrix,
    .set_lcd_icon       = hal_set_lcd_icon,
    .set_frequency      = hal_set_frequency,
    .play_frequency     = hal_play_frequency,
    .handler            = hal_handler,
};

/* ---- display ---------------------------------------------------------- */

static SDL_Window   *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture  *g_tex;
static int           g_scale = DEFAULT_SCALE;
static uint32_t      g_px[TEX_W_MAX * TEX_H];

static void put(int x, int y, uint32_t c)
{
    if (x < 0 || y < 0 || x >= g_tex_w || y >= g_tex_h) return;
    g_px[y * g_tex_w + x] = c;
}

static void fill(int x0, int y0, int w, int h, uint32_t c)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            put(x0 + x, y0 + y, c);
}

static void hal_update_screen(void)
{
    fill(0, 0, g_tex_w, g_tex_h, COL_BEZEL);

    /* Dot matrix, each dot ICON_CELL square. */
    for (int y = 0; y < LCD_H; y++) {
        for (int x = 0; x < g_panel_w / ICON_CELL; x++) {
            int px = GAP + x * ICON_CELL;
            int py = GAP + y * ICON_CELL;
            fill(px, py, ICON_CELL - 1, ICON_CELL - 1,
                 g_matrix[x][y] ? COL_ON : COL_OFF);
        }
    }

    /* Icon strip under the matrix. The icons are eight single-bit indicators;
     * drawn as filled cells with a gap so all eight are distinguishable at a
     * glance. Their real shapes are bespoke artwork and are not reproduced. */
    int iy = GAP + LCD_H * ICON_CELL + 2;
    for (int i = 0; i < ICON_N; i++) {
        int ix = GAP + i * (g_panel_w / ICON_N) + 2;
        int iw = (g_panel_w / ICON_N) - 5;
        fill(ix, iy, iw, ICON_CELL - 2, g_icons[i] ? COL_ON : COL_ICON_BG);
    }

    SDL_UpdateTexture(g_tex, NULL, g_px, g_tex_w * (int)sizeof(uint32_t));
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
    SDL_RenderPresent(g_ren);
}

static void display_init(void)
{
    g_panel_w = hw_get_pixel_width() * ICON_CELL;
    g_tex_w = g_panel_w + GAP * 2;
    g_tex_h = TEX_H;
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    g_win = SDL_CreateWindow("Tamagotchi P1",
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             g_tex_w * g_scale, g_tex_h * g_scale,
                             SDL_WINDOW_RESIZABLE);
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, 0);
    SDL_RenderSetLogicalSize(g_ren, g_tex_w, g_tex_h);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, g_tex_w, g_tex_h);
    SDL_SetTextureScaleMode(g_tex, SDL_ScaleModeNearest);
}

/* ---- input ------------------------------------------------------------ */

static void key_event(SDL_Keycode k, int down)
{
    btn_state_t st = down ? BTN_STATE_PRESSED : BTN_STATE_RELEASED;

    switch (k) {
    case SDLK_a: case SDLK_LEFT:  tamalib_set_button(BTN_LEFT,   st); break;
    case SDLK_s: case SDLK_DOWN:  tamalib_set_button(BTN_MIDDLE, st); break;
    case SDLK_d: case SDLK_RIGHT: tamalib_set_button(BTN_RIGHT,  st); break;
    case SDLK_SPACE:              tamalib_set_button(BTN_TAP,    st); break;
    default: break;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <rom.bin> [--scale N]\n", argv[0]);
        return 2;
    }
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--scale") && i + 1 < argc) {
            int s = atoi(argv[++i]);
            if (s >= 1 && s <= 40) g_scale = s;
        }
    }

    u32_t words = 0;
    u12_t *prog = load_rom(argv[1], &words);
    if (!prog) return 1;

    /* Pick the segment map from the filename. The Tamagotchi family (P1, P2,
     * Angel, Mothra) all share one measured map; Digimon is wired differently.
     * Pass --digimon to force it for a differently-named dump. */
    {
        const char *base = strrchr(argv[1], '/');
        base = base ? base + 1 : argv[1];
        const char *mapname = "tamagotchi";
        hw_segmap_t map = HW_SEGMAP_TAMAGOTCHI;
        if (strstr(base, "digimon")) { map = HW_SEGMAP_DIGIMON; mapname = "digimon"; }
        else if (strstr(base, "alienfev")) { map = HW_SEGMAP_IDENTITY; mapname = "identity (39 wide)"; }
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "--digimon")) { map = HW_SEGMAP_DIGIMON; mapname = "digimon"; }
            if (!strcmp(argv[i], "--identity")) { map = HW_SEGMAP_IDENTITY; mapname = "identity (39 wide)"; }
        }
        tamalib_set_segmap(map);
        fprintf(stderr, "segmap: %s\n", mapname);
    }
    if (!prog) return 1;
    fprintf(stderr, "rom: %u words (%u bytes)\n", words, words * 2);

    g_hal_impl.update_screen = hal_update_screen;
    tamalib_register_hal(&g_hal_impl);

    /* 1 MHz timestamps, so a microsecond resolution and ~71 minute wrap. */
    if (tamalib_init(prog, NULL, 1000000)) {
        fprintf(stderr, "tamalib_init failed\n");
        return 1;
    }
    tamalib_set_framerate(30);

    display_init();

    SDL_InitSubSystem(SDL_INIT_AUDIO);
    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof(want));
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_cb;
    SDL_AudioDeviceID adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (adev) SDL_PauseAudioDevice(adev, 0);
    else fprintf(stderr, "audio unavailable: %s\n", SDL_GetError());

    fprintf(stderr,
        "ready: 32x16 + 8 icons. A/S/D or arrows = buttons, space = tap,\n"
        "       F1/F2 scale, Esc quit\n");

    tamalib_set_speed(1);          /* realtime, paced by TamaLib itself */
    tamalib_set_exec_mode(EXEC_MODE_RUN);

    timestamp_t g_next_render = hal_get_timestamp();

    while (!g_quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) g_quit = 1;
            else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                int down = (e.type == SDL_KEYDOWN);
                if (e.key.repeat) continue;
                SDL_Keycode k = e.key.keysym.sym;
                if (down && k == SDLK_ESCAPE) { g_quit = 1; break; }
                if (down && (k == SDLK_F1 || k == SDLK_F2)) {
                    g_scale += (k == SDLK_F2) ? 2 : -2;
                    if (g_scale < 2) g_scale = 2;
                    if (g_scale > 40) g_scale = 40;
                    SDL_SetWindowSize(g_win, g_tex_w * g_scale, g_tex_h * g_scale);
                    continue;
                }
                key_event(k, down);
            }
        }

        /* Let TamaLib pace itself.
         *
         * cpu_set_speed(1) makes cpu_step() call sleep_until() for the exact
         * real time each instruction should take, which is what keeps the pet's
         * clock honest. An earlier version also paced the render loop on its own
         * deadline, so the two fought each other and the machine ran at roughly
         * the wrong rate. Here the only job of this loop is to step and to
         * decide when to draw.
         *
         * Rendering at 30 fps is independent of emulation speed: a frame is
         * drawn when 1/30 s of real time has passed, no matter how many
         * instructions fitted in between. */
        tamalib_step();

        timestamp_t now = hal_get_timestamp();
        if ((int32_t)(now - g_next_render) >= 0) {
            hal_update_screen();
            g_next_render = now + 1000000u / 30u;
        }
    }

    tamalib_release();
    if (adev) SDL_CloseAudioDevice(adev);
    SDL_DestroyTexture(g_tex);
    SDL_DestroyRenderer(g_ren);
    SDL_DestroyWindow(g_win);
    SDL_Quit();
    free(prog);
    return 0;
}
