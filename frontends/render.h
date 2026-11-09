/*
 * render.h - draw the LCD segment map into an RGB565 buffer
 *
 * The segment map is a list of per-segment horizontal runs, produced by
 * gen_segmap.py from the SVG artwork. Drawing a lit segment means filling its
 * runs; an unlit segment is left as background.
 *
 * Do not go back to bounding boxes. The artwork uses H/V path commands and
 * cubic curves, so a naive box over the raw coordinates produces garbage --
 * that mistake showed up as a grid of lines across the display.
 */

#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>
#include "segmap.h"

#define LCD_INK   0x0000
#define LCD_OFF   0xC618

typedef struct {
    uint16_t *px;
    int w, h;
} lcd_fb;

static inline void lcd_put(const lcd_fb *fb, int x, int y, uint16_t c)
{
    if (x >= 0 && x < fb->w && y >= 0 && y < fb->h)
        fb->px[y * fb->w + x] = c;
}

static inline void lcd_rect(const lcd_fb *fb, int x0, int y0, int x1, int y1, uint16_t c)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > fb->w) x1 = fb->w;
    if (y1 > fb->h) y1 = fb->h;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            fb->px[y * fb->w + x] = c;
}

/* Draw the LCD with its top-left corner at (ox, oy). */
static inline void lcd_draw(const lcd_fb *fb, const uint8_t *dpram, int ox, int oy)
{
    lcd_rect(fb, ox, oy, ox + SEG_W, oy + SEG_H, LCD_OFF);

    for (int i = 0; i < SEG_COUNT; i++) {
        const seg_entry *e = &seg_map[i];
        if (!((dpram[e->byte] >> e->bit) & 1))
            continue;                       /* unlit: leave the background */
        const seg_run *r = &seg_runs[e->run_start];
        for (int k = 0; k < e->run_count; k++, r++)
            lcd_rect(fb, ox + r->x0, oy + r->y, ox + r->x1, oy + r->y + 1, LCD_INK);
    }
}

#endif /* RENDER_H */
