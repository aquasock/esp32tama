/*
 * e0c6200.c - Epson E0C6200 4-bit CPU core, machine layer
 *
 * Memory map, register file, port model and reset. Verified against
 * BrickEmuPy's post-reset state; the instruction table is not yet ported.
 *
 * Memory is nibble-addressed:
 *
 *   0x000-0x2FF   768 nibbles of RAM
 *   0xE00-0xE4F   display RAM, first half
 *   0xE80-0xECF   display RAM, second half
 *   0xF00-0xF7E   I/O registers
 *
 * Ports carry four masks each in the reference: direction, latch, pull-up and
 * push-pull, with the reset values differing per port (R0-R4 default to input,
 * P0-P3 to output). Getting those wrong changes what the firmware reads on its
 * first I/O access, so they are reproduced exactly.
 */

#include "e0c6200.h"
#include <stdlib.h>
#include <string.h>

struct e0c6200 {
    const uint8_t *rom;
    size_t         rom_len;
    uint32_t       clock_hz;

    uint8_t  ram[E0C_RAM_SIZE];
    uint8_t  vram[E0C_VRAM_SIZE];

    uint16_t pc, npc;
    uint8_t  a, b, ix, iy, sp;
    uint8_t  cf, zf, df, if_;
    uint8_t  halt;

    uint8_t  pdir[E0C_PORT_COUNT];
    uint8_t  platch[E0C_PORT_COUNT];
    uint8_t  pullup[E0C_PORT_COUNT];
    uint8_t  pushpull[E0C_PORT_COUNT];
    uint8_t  pin_lo[E0C_PORT_COUNT];
    uint8_t  pin_hi[E0C_PORT_COUNT];

    /* interrupt enables and request latches */
    uint8_t  it, isw, ipt, isio, ik0, ik1;
    uint8_t  eit, eisw, eipt, eisio, eik0, eik1;

    uint8_t  tm, swl, swh, pt, rd, sd;
    uint8_t  dfk0, ctrl_osc, ctrl_lcd, lc, ctrl_svd;
    uint8_t  ctrl_bz1, ctrl_bz2, ctrl_sw, ctrl_pt, ptc, sc, hzr, ioc, pup;

    uint32_t instrs;
    double   osc1_counter, timer_counter, ptimer_counter, stopwatch_counter;
};

/* Reset table, in port index order. From the reference's _init_registers. */
static const uint8_t reset_pdir[E0C_PORT_COUNT]     = { 0,0, 0xF,0xF,0xF,0xF,0xF, 0,0,0,0 };
static const uint8_t reset_pullup[E0C_PORT_COUNT]   = { 0,0, 0,0,0,0,0,         0xF,0xF,0xF,0xF };
static const uint8_t reset_pushpull[E0C_PORT_COUNT] = { 0,0, 0,0,0,0,0,         0xF,0xF,0xF,0xF };

/* IO_ALOFF / IO_SVDDT from the reference. */
#define E0C_ALOFF 0x00
#define E0C_SVDDT 0x00

void e0c_reset(e0c6200 *c)
{
    memset(c->ram, 0, sizeof(c->ram));
    memset(c->vram, 0, sizeof(c->vram));

    c->a = c->b = c->ix = c->iy = c->sp = 0;
    c->pc = c->npc = 0x100;
    c->cf = c->zf = c->df = c->if_ = 0;
    c->halt = 0;

    memcpy(c->pdir,     reset_pdir,     sizeof(c->pdir));
    memcpy(c->pullup,   reset_pullup,   sizeof(c->pullup));
    memcpy(c->pushpull, reset_pushpull, sizeof(c->pushpull));
    memset(c->platch, 0, sizeof(c->platch));
    memset(c->pin_lo, 0, sizeof(c->pin_lo));
    memset(c->pin_hi, 0, sizeof(c->pin_hi));

    c->it = c->isw = c->ipt = c->isio = c->ik0 = c->ik1 = 0;
    c->eit = c->eisw = c->eipt = c->eisio = c->eik0 = c->eik1 = 0;

    c->tm = c->swl = c->swh = c->pt = c->rd = c->sd = 0;
    c->dfk0 = 0xF;
    c->ctrl_osc = 0;
    c->ctrl_lcd = E0C_ALOFF;
    c->lc = 0;
    c->ctrl_svd = E0C_SVDDT;
    c->ctrl_bz1 = c->ctrl_bz2 = c->ctrl_sw = c->ctrl_pt = 0;
    c->ptc = c->sc = c->hzr = c->ioc = c->pup = 0;

    c->osc1_counter = 0;
    c->timer_counter = 0;
    c->ptimer_counter = 0;
    c->stopwatch_counter = 0;
    c->instrs = 0;
}

e0c6200 *e0c_new(const uint8_t *rom, size_t len, uint32_t clock_hz)
{
    if (!rom || len < 2) return NULL;
    e0c6200 *c = (e0c6200 *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->rom = rom;
    c->rom_len = len;
    c->clock_hz = clock_hz;
    e0c_reset(c);
    return c;
}

void e0c_free(e0c6200 *c) { free(c); }

uint8_t e0c_get_mem(e0c6200 *c, uint16_t addr)
{
    if (addr < E0C_RAM_SIZE)
        return c->ram[addr] & 0x0F;

    if (addr >= E0C_VRAM_PART1 && addr < E0C_VRAM_PART1 + E0C_VRAM_PART_SIZE)
        return c->vram[addr - E0C_VRAM_PART1] & 0x0F;

    if (addr >= E0C_VRAM_PART2 && addr < E0C_VRAM_PART2 + E0C_VRAM_PART_SIZE)
        return c->vram[addr - E0C_VRAM_PART2 + E0C_VRAM_PART_SIZE] & 0x0F;

    /* I/O registers are not implemented yet; reads return 0 as the reference
     * does for an unmapped I/O address. */
    return 0;
}

void e0c_set_mem(e0c6200 *c, uint16_t addr, uint8_t value)
{
    value &= 0x0F;

    if (addr < E0C_RAM_SIZE) { c->ram[addr] = value; return; }

    if (addr >= E0C_VRAM_PART1 && addr < E0C_VRAM_PART1 + E0C_VRAM_PART_SIZE) {
        c->vram[addr - E0C_VRAM_PART1] = value;
        return;
    }
    if (addr >= E0C_VRAM_PART2 && addr < E0C_VRAM_PART2 + E0C_VRAM_PART_SIZE) {
        c->vram[addr - E0C_VRAM_PART2 + E0C_VRAM_PART_SIZE] = value;
        return;
    }
    /* I/O writes are not implemented yet. */
}

void e0c_snapshot(const e0c6200 *c, e0c_state *s)
{
    memset(s, 0, sizeof(*s));
    s->pc = c->pc;
    s->npc = c->npc;
    s->a = c->a; s->b = c->b; s->ix = c->ix; s->iy = c->iy; s->sp = c->sp;
    s->cf = c->cf; s->zf = c->zf; s->df = c->df; s->if_ = c->if_;
    memcpy(s->ram, c->ram, sizeof(s->ram));
    memcpy(s->vram, c->vram, sizeof(s->vram));
    memcpy(s->pdir, c->pdir, sizeof(s->pdir));
    memcpy(s->platch, c->platch, sizeof(s->platch));
    memcpy(s->pullup, c->pullup, sizeof(s->pullup));
    memcpy(s->pushpull, c->pushpull, sizeof(s->pushpull));
}

const uint8_t *e0c_vram(const e0c6200 *c) { return c->vram; }
