/*
 * gplb.h - GeneralPlus GPLB52640A / GPLB52320A core
 *
 * The chip in the TamaTown Tama-Go (2010). A standard 6502 with GeneralPlus
 * peripherals: 1536 bytes RAM, 320 KB or 640 KB ROM, 6 MHz, a 64x32 dot LCD
 * with 4 grey levels, an 8-channel SPU and two 16-bit timers.
 *
 * CPU and memory map are implemented. Timers, LCD driver and SPU are not.
 */

#ifndef GPLB_H
#define GPLB_H

#include <stdint.h>
#include <stddef.h>

#define GPLB_CLOCK_HZ      6000000u

#define GPLB_RAM_BASE      0x0000
#define GPLB_RAM_SIZE      0x0600     /* 1536 bytes */
#define GPLB_DPRAM_BASE    0x1000
#define GPLB_DPRAM_SIZE    0x0800
#define GPLB_IO_BASE       0x3000
#define GPLB_IO_SIZE       0x1000
#define GPLB_ROM_WIN_LO    0x4000
#define GPLB_ROM_WIN_HI    0x8000
#define GPLB_ROM_FIXED     0xC000
#define GPLB_ROM_BANK_SIZE 0x8000

#define GPLB_LCD_SEGMENTS  64
#define GPLB_LCD_COMMONS   32
#define GPLB_LCD_DOTS      (GPLB_LCD_SEGMENTS * GPLB_LCD_COMMONS)

typedef struct gplb gplb;

typedef struct {
    uint16_t pc;
    uint8_t  a, x, y, sp, p;
    uint32_t rom_bank;
    uint64_t instrs, cycles;
    uint8_t  ram[GPLB_RAM_SIZE];
    uint8_t  dpram[GPLB_DPRAM_SIZE];
} gplb_state;

gplb *gplb_new(const uint8_t *rom, size_t len);
void  gplb_free(gplb *c);
void  gplb_reset(gplb *c);

uint32_t gplb_step(gplb *c);
uint64_t gplb_run_cycles(gplb *c, uint64_t n);
uint64_t gplb_run_instrs(gplb *c, uint64_t n);

void gplb_snapshot(const gplb *c, gplb_state *s);
const uint8_t *gplb_dpram(const gplb *c);
uint8_t *gplb_io_regs(gplb *c);
uint8_t  gplb_peek(gplb *c, uint32_t addr);

#endif
