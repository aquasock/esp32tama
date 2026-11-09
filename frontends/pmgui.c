/*
 * pmgui.c - SDL2 desktop front end for the Pokémon Mini core
 *
 * The reference build: it runs the vendored PokeMini core on a normal Linux
 * desktop so that behaviour can be checked by eye before anything is ported to
 * the watch. Same role tamagui.c plays for the Tamagotchi V3.
 *
 *   ./pmgui <bios> <cartridge> [--scale N]
 *
 * <cartridge> may be a bare .min or a .zip containing one, which is how the
 * MAME/MiSTer style sets ship. ZIP support lives here rather than in the core
 * so that the embedded build does not have to carry zlib.
 *
 * Keys
 *   arrows      d-pad
 *   Z           A
 *   X           B
 *   C           C
 *   Enter       A (same as Z)
 *   P           Power - not Enter, see key_event()
 *   F5 / F9     save / load state
 *   F1 / F2     window smaller / larger
 *   M           mute
 *   Esc         quit
 *
 * Frames are paced at the panel's real 72 Hz. The core is advanced in fixed
 * 55,556-cycle slices for the same reason the benchmark does it: the axis is
 * emulated time, not wall time, so a slow host runs behind rather than
 * distorting the machine.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include <SDL2/SDL.h>
#include <zlib.h>

#include "PokeMini.h"

extern int PokeMini_EmulateCycles(int);

#define PM_CLOCK_HZ       4000000
#define PM_FRAME_HZ       72
#define PM_CYCLES_PER_FRAME (PM_CLOCK_HZ / PM_FRAME_HZ)
#define PM_SCREEN_W       96
#define PM_SCREEN_H       64
#define PM_AUDIO_HZ       44100
#define PM_SOUND_FIFO     8192

#define DEFAULT_SCALE     6

/* ---- ZIP support -------------------------------------------------------
 * The sets people have are .zip files holding a single .min. Reading one here
 * keeps the core free of zlib, which matters on the target where flash is the
 * scarcest resource. This walks the central directory, finds the first entry
 * ending in .min, and inflates it with zlib's raw-deflate path.
 * ----------------------------------------------------------------------- */
static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static uint8_t *zip_extract_min(const uint8_t *zip, size_t zlen, size_t *out_len)
{
    /* Find End Of Central Directory: signature 0x06054b50, scanned backwards
     * because it sits at the very end and the comment length varies. */
    if (zlen < 22) return NULL;
    size_t eocd = 0;
    for (size_t i = zlen - 22 + 1; i-- > 0; ) {
        if (le32(zip + i) == 0x06054b50) { eocd = i; break; }
    }
    if (!eocd) return NULL;

    uint16_t entries  = le16(zip + eocd + 10);
    uint32_t cd_off   = le32(zip + eocd + 16);
    if (cd_off >= zlen) return NULL;

    size_t p = cd_off;
    for (uint16_t n = 0; n < entries; n++) {
        if (p + 46 > zlen) return NULL;
        if (le32(zip + p) != 0x02014b50) return NULL;

        uint16_t method   = le16(zip + p + 10);
        uint32_t csize    = le32(zip + p + 20);
        uint32_t usize    = le32(zip + p + 24);
        uint16_t name_len = le16(zip + p + 28);
        uint16_t ext_len  = le16(zip + p + 30);
        uint16_t cmt_len  = le16(zip + p + 32);
        uint32_t lho      = le32(zip + p + 42);

        const char *name = (const char *)(zip + p + 46);
        size_t nlen = name_len;
        int is_min = (nlen >= 4 &&
                      (name[nlen-1] == 'n' || name[nlen-1] == 'N') &&
                      (name[nlen-2] == 'i' || name[nlen-2] == 'I') &&
                      (name[nlen-3] == 'm' || name[nlen-3] == 'M') &&
                      name[nlen-4] == '.');

        if (is_min && usize) {
            if (lho + 30 > zlen) return NULL;
            uint16_t lname = le16(zip + lho + 26);
            uint16_t lext  = le16(zip + lho + 28);
            size_t data = (size_t)lho + 30 + lname + lext;
            if (data + csize > zlen) return NULL;

            uint8_t *out = malloc(usize);
            if (!out) return NULL;

            if (method == 0) {                       /* stored */
                memcpy(out, zip + data, usize);
            } else if (method == 8) {                /* deflate */
                z_stream s;
                memset(&s, 0, sizeof(s));
                if (inflateInit2(&s, -MAX_WBITS) != Z_OK) { free(out); return NULL; }
                s.next_in  = (Bytef *)(zip + data);
                s.avail_in = csize;
                s.next_out = out;
                s.avail_out = usize;
                int rc = inflate(&s, Z_FINISH);
                inflateEnd(&s);
                if (rc != Z_STREAM_END) { free(out); return NULL; }
            } else {
                fprintf(stderr, "zip: compression method %u not supported\n", method);
                free(out);
                return NULL;
            }
            *out_len = usize;
            return out;
        }
        p += 46 + name_len + ext_len + cmt_len;
    }
    return NULL;
}

static uint8_t *load_cartridge(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fprintf(stderr, "%s: empty\n", path); fclose(f); return NULL; }

    uint8_t *buf = malloc((size_t)n);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "%s: read failed\n", path); fclose(f); free(buf); return NULL;
    }
    fclose(f);

    if (n >= 4 && buf[0] == 'P' && buf[1] == 'K') {
        size_t outn = 0;
        uint8_t *out = zip_extract_min(buf, (size_t)n, &outn);
        free(buf);
        if (!out) { fprintf(stderr, "%s: no .min inside the archive\n", path); return NULL; }
        fprintf(stderr, "zip: extracted %zu bytes\n", outn);
        *len = outn;
        return out;
    }

    *len = (size_t)n;
    return buf;
}

/* ---- display ---------------------------------------------------------- */
static SDL_Window   *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture  *g_tex;
static int  g_scale = DEFAULT_SCALE;
/* The panel is monochrome, but drawing it as pure white is a trap: an
 * all-unlit screen then looks like a dead window rather than a working
 * emulator showing nothing. A soft off-white inside a dark bezel makes the
 * difference obvious at a glance. */
#define PM_OFF   0xFFC8C8BEu    /* unlit dot */
#define PM_ON    0xFF16161Au    /* lit dot */
#define PM_BEZEL 0xFF2A2A30u    /* surround, so the panel edge is visible */
#define PM_GAP   6              /* bezel thickness in panel pixels */

#define PM_TEX_W (PM_SCREEN_W + PM_GAP * 2)
#define PM_TEX_H (PM_SCREEN_H + PM_GAP * 2)
static uint32_t g_px[PM_TEX_W * PM_TEX_H];

static void display_init(void)
{
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    g_win = SDL_CreateWindow("Pokémon Mini",
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             PM_TEX_W * g_scale, PM_TEX_H * g_scale,
                             SDL_WINDOW_RESIZABLE);
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, 0);
    SDL_RenderSetLogicalSize(g_ren, PM_TEX_W, PM_TEX_H);
    /* Nearest neighbour: 96x64 is a dot matrix and should stay one. */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, PM_TEX_W, PM_TEX_H);
    SDL_SetTextureScaleMode(g_tex, SDL_ScaleModeNearest);
}

/* Draw LCDPixelsD (one byte per dot, non-zero = lit) at four shades.
 * The core keeps a decayed intensity array in LCDPixelsA for the "fading dot"
 * effect; using LCDPixelsD here keeps the reference crisp and readable, which
 * is what a reference is for. */
static int g_blank_streak;      /* consecutive frames with nothing lit */

static void display_frame(const uint8_t *dots)
{
    for (int y = 0; y < PM_TEX_H; y++) {
        for (int x = 0; x < PM_TEX_W; x++) {
            int sx = x - PM_GAP, sy = y - PM_GAP;
            uint32_t c = PM_BEZEL;
            if (sx >= 0 && sx < PM_SCREEN_W && sy >= 0 && sy < PM_SCREEN_H)
                c = dots[sy * PM_SCREEN_W + sx] ? PM_ON : PM_OFF;
            g_px[y * PM_TEX_W + x] = c;
        }
    }

    /* If the panel stays empty the emulator is still alive and the window is
     * still updating - say so, so a blank screen is not mistaken for a hang. */
    int lit = 0;
    for (int i = 0; i < PM_SCREEN_W * PM_SCREEN_H; i++)
        if (dots[i]) { lit = 1; break; }
    if (!lit) {
        if (++g_blank_streak == 90)
            fprintf(stderr, "display: 90 frames with nothing lit (still running)\n");
    } else {
        g_blank_streak = 0;
    }

    SDL_UpdateTexture(g_tex, NULL, g_px, PM_TEX_W * (int)sizeof(uint32_t));
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
    SDL_RenderPresent(g_ren);
}

/* ---- input ------------------------------------------------------------ */
/* PokeMini's key bits, from Hardware/Keyboard: these are the KEY_* constants
 * the BIOS reads from the I/O port. They are active low in the register, and
 * PokeMini_KeypadEvent takes the pressed state. */
static void key_event(SDL_Keycode k, int down)
{
    uint8_t key = 0;

    switch (k) {
    case SDLK_UP:     key = 0x04; break;
    case SDLK_DOWN:   key = 0x08; break;
    case SDLK_LEFT:   key = 0x10; break;
    case SDLK_RIGHT:  key = 0x20; break;
    case SDLK_z:      key = 0x02; break;   /* A */
    case SDLK_x:      key = 0x01; break;   /* B */
    case SDLK_c:      key = 0x40; break;   /* C */
    /* Power is not on Enter. On a real console Power suspends the machine, so
     * binding the key everyone reaches for first blanks the screen and looks
     * like a crash. Enter duplicates A, which is what "start" means. */
    case SDLK_RETURN: key = 0x02; break;   /* A */
    case SDLK_p:      key = 0x80; break;   /* Power, deliberately awkward */
    default: return;
    }
    PokeMini_KeypadEvent(key, down);
}

/* ---- state slots ------------------------------------------------------
 * PokeMini stores a full snapshot as a file, and takes the cartridge path so
 * the state can be matched to the ROM it came from.
 * ----------------------------------------------------------------------- */
static const char *g_rom_path;
static char        g_state_path[1024];

static void state_path_init(void)
{
    snprintf(g_state_path, sizeof(g_state_path), "%s.state", g_rom_path);
}

static void save_state(void)
{
    if (PokeMini_SaveSSFile(g_state_path, g_rom_path) > 0)
        fprintf(stderr, "state saved: %s\n", g_state_path);
    else
        fprintf(stderr, "state save failed\n");
}

static void load_state(void)
{
    if (PokeMini_LoadSSFile(g_state_path) > 0)
        fprintf(stderr, "state loaded: %s\n", g_state_path);
    else
        fprintf(stderr, "state load failed (no %s)\n", g_state_path);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr,
            "usage: %s <bios> <cartridge> [--scale N]\n"
            "  <cartridge> may be .min or a .zip containing one\n", argv[0]);
        return 2;
    }

    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--scale") && i + 1 < argc) {
            int s = atoi(argv[++i]);
            if (s >= 1 && s <= 16) g_scale = s;
        }
    }

    /* BIOS first: PokeMini_Create fills PM_BIOS with 0xFF and loads the free
     * BIOS, so a real one has to be loaded afterwards to replace it. */
    if (!PokeMini_Create(POKEMINI_GENSOUND, PM_SOUND_FIFO)) {
        fprintf(stderr, "PokeMini_Create failed\n");
        return 1;
    }
    if (PokeMini_LoadBIOSFile(argv[1]) <= 0) {
        fprintf(stderr, "%s: could not load BIOS\n", argv[1]);
        return 1;
    }

    size_t rom_len = 0;
    uint8_t *rom = load_cartridge(argv[2], &rom_len);
    if (!rom) return 1;
    if (!PokeMini_SetMINMem(rom, (int)rom_len)) {
        fprintf(stderr, "%s: not a cartridge image\n", argv[2]);
        return 1;
    }
    /* SetMINMem only points the core at the image; without a reset the CPU
     * stays wherever Create left it and the cartridge never starts. */
    PokeMini_Reset(0);
    g_rom_path = argv[2];
    state_path_init();

    display_init();

    SDL_InitSubSystem(SDL_INIT_AUDIO);
    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof(want));
    want.freq = PM_AUDIO_HZ;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = NULL;               /* using a queued device instead */
    SDL_AudioDeviceID adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (adev) {
        MinxAudio_ChangeEngine(1);
        SDL_PauseAudioDevice(adev, 0);
    } else {
        fprintf(stderr, "audio unavailable: %s\n", SDL_GetError());
    }

    fprintf(stderr,
        "ready: %dx%d at %dx, %d Hz audio\n"
        "arrows d-pad, Z/Enter=A X=B C=C, P=Power, M=mute, Esc=quit\n",
        PM_SCREEN_W, PM_SCREEN_H, g_scale, PM_AUDIO_HZ);

    int running = 1, muted = 0;
    /* Pace in microseconds. 1000/72 truncates to 13 ms, which is 76.9 Hz - the
     * emulator then runs 7% fast and the audio buffer drains against it. */
    const uint64_t tick_us = 1000000u / PM_FRAME_HZ;
    uint64_t next_tick = (uint64_t)SDL_GetTicks() * 1000u;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                int down = (e.type == SDL_KEYDOWN);
                if (e.key.repeat) continue;
                SDL_Keycode k = e.key.keysym.sym;

                if (down && k == SDLK_ESCAPE) { running = 0; break; }
                if (down && k == SDLK_m) {
                    muted = !muted;
                    if (adev) SDL_PauseAudioDevice(adev, muted);
                    continue;
                }
                if (down && k == SDLK_F5) { save_state(); continue; }
                if (down && k == SDLK_F9) { load_state(); continue; }
                if (down && (k == SDLK_F1 || k == SDLK_F2)) {
                    g_scale += (k == SDLK_F2) ? 1 : -1;
                    if (g_scale < 1) g_scale = 1;
                    if (g_scale > 16) g_scale = 16;
                    SDL_SetWindowSize(g_win, PM_TEX_W * g_scale, PM_TEX_H * g_scale);
                    continue;
                }
                key_event(k, down);
            }
        }

        PokeMini_EmulateCycles(PM_CYCLES_PER_FRAME);

        if (adev && !muted) {
            /* Top the queue up rather than trying to match it exactly: the
             * emulated frame rate and the audio rate are not related, so any
             * exactness here would be luck. Keep about two frames queued. */
            Uint32 queued = SDL_GetQueuedAudioSize(adev);
            Uint32 target = (Uint32)(PM_AUDIO_HZ * 2 * 2 / PM_FRAME_HZ) * 3;
            if (queued < target) {
                static int16_t buf[2048 * 2];
                Uint32 want_bytes = target - queued;
                if (want_bytes > sizeof(buf)) want_bytes = sizeof(buf);
                /* MinxAudio_GetSamplesS16 takes a count of int16 *values*, not
                 * of frames: the reference SDL front end passes `len >> 1` for
                 * `len` bytes. Passing want_bytes/4 fills only half the buffer
                 * and queues the other half as uninitialised memory. */
                MinxAudio_GetSamplesS16(buf, (int)(want_bytes / sizeof(int16_t)));
                SDL_QueueAudio(adev, buf, want_bytes);
            }
        }

        display_frame(LCDPixelsD);

        /* Pace to 72 Hz without drifting: add a tick to the deadline instead of
         * sleeping from now, so a slow frame does not push every later frame
         * back. If we fall more than a frame behind, resynchronise. */
        next_tick += tick_us;
        uint64_t now = (uint64_t)SDL_GetTicks() * 1000u;
        if (now < next_tick) {
            SDL_Delay((Uint32)((next_tick - now) / 1000u));
        } else if (now - next_tick > tick_us * 2) {
            next_tick = now;                  /* fell far behind: resynchronise */
        }
    }

    if (adev) SDL_CloseAudioDevice(adev);
    PokeMini_Destroy();
    SDL_DestroyTexture(g_tex);
    SDL_DestroyRenderer(g_ren);
    SDL_DestroyWindow(g_win);
    SDL_Quit();
    free(rom);
    return 0;
}
