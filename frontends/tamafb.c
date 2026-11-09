/*
 * tamafb.c - run the V3 ROM and draw its LCD to /dev/fb0
 *
 *   ./tamafb             # 10 seconds of animation
 *   ./tamafb 30          # 30 seconds
 *   ./tamafb 10 0.5      # 10 s, render at 2 fps (default 4 fps)
 *
 * This is the demo: the Tamagotchi runs, and you watch the egg bounce on the
 * watch. It is also a real measurement — it prints emulated cycles per second
 * and the realtime factor at the end.
 *
 * Framebuffer geometry comes from Escapement's README: 320x400 RGB565 at
 * /dev/fb0. If that is wrong the program says so and exits rather than
 * scribbling over the console.
 */

#include "splb32.h"
#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

#ifdef EMBED_ROM
extern const unsigned char rom_embed[];
extern const unsigned int rom_embed_len;
#endif

/* Sized to the segment map plus a margin, so SEG_RES can change freely. */
#define FB_W (SEG_W + 32)
#define FB_H (SEG_H + 48)

/* Colours: RGB565. Dark ink on a pale LCD. */
#define COL_INK   0x0000      /* lit segment */
#define COL_OFF   0xC618      /* unlit LCD background */
#define COL_BEZEL 0x18E3      /* surround */

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void fill(uint16_t *fb, uint16_t c)
{
    for (int i = 0; i < FB_W * FB_H; i++) fb[i] = c;
}

/*
 * Band rendering.
 *
 * A whole 320x400 RGB565 frame is 256 KB and on a NOMMU target that has to be
 * one contiguous allocation, which after a few minutes of uptime does not
 * exist. Rendering in horizontal bands removes the problem: 40 rows is 25 KB,
 * always available, and each band is a self-contained write.
 */
#define BAND_H 40
static uint16_t g_band[FB_W * BAND_H];

/* Draw rows [by0, by0+BAND_H) of the LCD area into g_band. */
static void render_band(const uint8_t *dpram, int by0)
{
    const int ox = (FB_W - SEG_W) / 2;
    const int oy = (FB_H - SEG_H) / 2;

    for (int i = 0; i < FB_W * BAND_H; i++)
        g_band[i] = 0x18E3;                     /* bezel */

    /* LCD background, clipped to this band */
    for (int y = oy - 4; y < oy + SEG_H + 4; y++) {
        if (y < by0 || y >= by0 + BAND_H) continue;
        uint16_t *row = g_band + (size_t)(y - by0) * FB_W;
        for (int x = ox - 4; x < ox + SEG_W + 4; x++)
            if (x >= 0 && x < FB_W) row[x] = LCD_OFF;
    }

    for (int i = 0; i < SEG_COUNT; i++) {
        const seg_entry *e = &seg_map[i];
        if (!((dpram[e->byte] >> e->bit) & 1)) continue;
        const seg_run *r = &seg_runs[e->run_start];
        for (int k = 0; k < e->run_count; k++, r++) {
            int y = oy + r->y;
            if (y < by0 || y >= by0 + BAND_H) continue;
            uint16_t *row = g_band + (size_t)(y - by0) * FB_W;
            for (int x = ox + r->x0; x < ox + r->x1; x++)
                if (x >= 0 && x < FB_W) row[x] = LCD_INK;
        }
    }
}


int main(int argc, char **argv)
{
    double seconds = (argc > 1) ? atof(argv[1]) : 10.0;
    double fps     = (argc > 2) ? atof(argv[2]) : 4.0;

    const uint8_t *rom;
    size_t romlen;
#ifdef EMBED_ROM
    rom = rom_embed;
    romlen = rom_embed_len;
#else
    if (argc < 2) {
        fprintf(stderr, "usage: %s [seconds] [fps]  (built without EMBED_ROM: pass rom via rom.bin in cwd)\n", argv[0]);
        return 2;
    }
    static uint8_t rombuf[SPLB32_ROM_BYTES];
    FILE *f = fopen("rom.bin", "rb");
    if (!f) { perror("rom.bin"); return 1; }
    romlen = fread(rombuf, 1, sizeof(rombuf), f);
    fclose(f);
    rom = rombuf;
#endif

    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        perror("/dev/fb0");
        fprintf(stderr, "no framebuffer - nothing to draw on\n");
        return 1;
    }

    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) != 0 || ioctl(fd, FBIOGET_VSCREENINFO, &var) != 0) {
        fprintf(stderr, "fb ioctl failed\n");
        close(fd);
        return 1;
    }
    printf("fb: %ux%u, %u bpp, line %u\n", var.xres, var.yres, var.bits_per_pixel, fix.line_length);
    if (var.bits_per_pixel != 16) {
        fprintf(stderr, "expected 16 bpp RGB565, got %u - refusing to draw\n", var.bits_per_pixel);
        close(fd);
        return 1;
    }
    if (var.xres < FB_W || var.yres < FB_H) {
        fprintf(stderr, "fb is %ux%u, need at least %dx%d - refusing to draw\n",
                var.xres, var.yres, FB_W, FB_H);
        close(fd);
        return 1;
    }

    size_t maplen = (size_t)fix.line_length * var.yres;
    uint16_t *fb = (uint16_t *)mmap(NULL, maplen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) { perror("mmap"); close(fd); return 1; }

    splb32 *cpu = splb32_new(rom, romlen);
    if (!cpu) { fprintf(stderr, "core init failed\n"); return 1; }
    /* Same peripherals the reference .brick wires up, so the ROM behaves as it
     * does on hardware rather than as a bare core would. */
    splb32_set_pullup(cpu, 3, 0x02);
    splb32_attach_eeprom(cpu, 0xA0, 2, 1);
    splb32_attach_ir(cpu, 0x04, 0x04);

    /* let it boot before the first frame */
    splb32_run_cycles(cpu, 8000000ull);

    fill(fb, COL_BEZEL);

    const uint64_t frame_cycles = (uint64_t)(SPLB32_CLOCK_HZ / fps);
    const uint64_t end_ns = now_ns() + (uint64_t)(seconds * 1e9);
    uint64_t frames = 0, c0 = 0;
    splb32_state st;

    splb32_snapshot(cpu, &st);
    c0 = st.cycles;

    while (now_ns() < end_ns) {
        splb32_run_cycles(cpu, frame_cycles);
        splb32_snapshot(cpu, &st);

        /* Paint band by band straight into the framebuffer. */
        {
            const int dx = ((int)var.xres - FB_W) / 2;
            const int dy = ((int)var.yres - FB_H) / 2;
            for (int by0 = 0; by0 < FB_H; by0 += BAND_H) {
                int rows = FB_H - by0;
                if (rows > BAND_H) rows = BAND_H;
                render_band(st.dpram, by0);

                for (int y = 0; y < rows; y++) {
                    int ty = by0 + y + dy;
                    if (ty < 0 || ty >= (int)var.yres) continue;
                    uint16_t *dst = fb + (size_t)ty * (fix.line_length / 2);
                    const uint16_t *src = g_band + (size_t)y * FB_W;
                    for (int x = 0; x < FB_W; x++) {
                        int tx = x + dx;
                        if (tx < 0 || tx >= (int)var.xres) continue;
                        dst[tx] = src[x];
                    }
                }
            }
        }
        frames++;
    }

    splb32_snapshot(cpu, &st);
    uint64_t dc = st.cycles - c0;
    double wall = seconds;

    printf("frames drawn      %llu\n", (unsigned long long)frames);
    printf("emulated cycles   %llu\n", (unsigned long long)dc);
    printf("emulated cycles/s %.0f\n", (double)dc / wall);
    printf("realtime factor   %.2fx   (target 4,000,000)\n", ((double)dc / wall) / 4000000.0);
    printf("instructions      %llu\n", (unsigned long long)st.instrs);
    printf("final pc          %04X\n", st.pc);

    munmap(fb, maplen);
    close(fd);
    splb32_free(cpu);
    return 0;
}
