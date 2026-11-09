/*
 * gplb.c - GeneralPlus GPLB52640A / GPLB52320A core
 *
 * The chip in the TamaTown Tama-Go (2010) and its siblings. A standard 6502
 * with GeneralPlus peripherals around it.
 *
 * Established 2026-11-09 by disassembling the Tama-Go firmware from its reset
 * vector ($CC00, per the datasheet) using the opcode table Natalie Silvanovich
 * published in natashenka/Tamagotchi-Hack/disassembler/: 151 opcodes decode
 * with zero undefined, and the startup sequence is textbook 6502 -- SEI, stack
 * setup, then stores to IO registers at $3000-$3FFF.
 *
 * The GPLB instruction set is the standard 6502 with standard opcode numbers.
 * Only 13 of 151 cycle counts differ from the SPLB32 core this was derived
 * from, and those 13 were deliberately reproduced reference bugs in that core.
 * Here they are the datasheet's values.
 *
 * Memory map (datasheet section 6.2):
 *   $0000-$05FF  GP RAM, 1536 bytes
 *   $1000-$17FF  display RAM
 *   $3000-$3FFF  IO registers
 *   $4000-$BFFF  ROM bank window, 32 KB
 *   $C000-$FFFF  fixed ROM, bank 0 low half; user program starts at $CC00
 *
 * STATUS: CPU and memory map. Timers, LCD driver and SPU are not modelled.
 */

#include "gplb.h"
#include <stdlib.h>
#include <string.h>

/* ---- cpu -------------------------------------------------------------- */
struct gplb {
    const uint8_t *rom;
    size_t         rom_len;

    uint8_t  ram[GPLB_RAM_SIZE];
    uint8_t  dpram[GPLB_DPRAM_SIZE];
    uint8_t  io[GPLB_IO_SIZE];

    uint32_t rom_bank;
    uint16_t pc;
    uint8_t  a, x, y, sp;
    uint8_t  nf, vf, bf, df, if_, zf, cf;

    uint8_t  ireq, int_enbl;
    uint8_t  page_crossed;   /* set by abs,X / abs,Y / (zp),Y addressing */
    uint8_t  branch_taken;   /* set by the branch instructions */

    uint64_t instrs, cycles;
};

#define SP_PAGE 0x0100

/* ---- flags ------------------------------------------------------------ */
static inline uint8_t get_ps(const gplb *c)
{
    /* Bits 4 and 5 both read as 1 in the pushed status byte on a real 6502.
     * Bit 5 is a constant, not a flag. Bit 4 is the B flag, and whilst it is
     * only meaningful for BRK/IRQ, PHP always pushes it set.
     *
     * Leaving either clear fails Klaus Dormann's functional test: $063D wants
     * $FF with all flags set, $0674 wants $30 with all flags clear. */
    return (uint8_t)(0x30 | (c->nf << 7) | (c->vf << 6) |
                     (c->df << 3) | (c->if_ << 2) | (c->zf << 1) | c->cf);
}

static inline void set_ps(gplb *c, uint8_t ps)
{
    c->nf = (ps >> 7) & 1; c->vf = (ps >> 6) & 1; c->bf = (ps >> 4) & 1;
    c->df = (ps >> 3) & 1; c->if_ = (ps >> 2) & 1;
    c->zf = (ps >> 1) & 1; c->cf = ps & 1;
}

/* ---- IO --------------------------------------------------------------- */
/* $3000 is the ROM bank register. Established from the firmware: $D294 loads
 * the bank number from zero page $28, stores it to $3000, then jumps into the
 * banked window ($D296 STA $3000, $D29B JSR $D2A9 -> JMP ($0026) -> $B383).
 *
 * The datasheet names $4000-$7FFF "(Bank)L" and $8000-$BFFF "(Bank)H", which
 * reads like two registers. The firmware treats them as two halves of one
 * 32 KB bank: bank 3 addressed at $B383 resolves to offset 0x7383 in bank 3.
 */
#define IO_ROM_BANK 0x0000   /* $3000 */

/* IO write trace, for working out register semantics from the firmware. */
#include <stdio.h>
int gplb_io_trace = 0;
static unsigned long gplb_io_writes = 0;

static void io_write_side_effects(gplb *c, uint16_t idx, uint8_t v)
{
    gplb_io_writes++;
    if (gplb_io_trace && gplb_io_writes < 400)
        fprintf(stderr, "IO %04X <- %02X\n", 0x3000 + idx, v);

    if (idx == IO_ROM_BANK) {
        uint32_t bank = (uint32_t)v * 0x8000u;
        if (0x4000u + bank < c->rom_len)
            c->rom_bank = bank;
    }
}

#ifdef GPLB_FLAT_RAM
/* Test-only: a flat 64 KB RAM, so a conformance suite can be loaded whole.
 * This validates the CPU in isolation from the memory map. */
uint8_t gplb_flat[0x10000];

static inline uint8_t read8(gplb *c, uint32_t addr)
{
    (void)c;
    return gplb_flat[addr & 0xFFFF];
}

static inline void write8(gplb *c, uint32_t addr, uint8_t v)
{
    (void)c;
    gplb_flat[addr & 0xFFFF] = v;
}
#else
static inline uint8_t read8(gplb *c, uint32_t addr)
{
    addr &= 0xFFFF;
    if (addr < GPLB_RAM_SIZE) return c->ram[addr];
    if (addr >= GPLB_DPRAM_BASE && addr < GPLB_DPRAM_BASE + GPLB_DPRAM_SIZE)
        return c->dpram[addr - GPLB_DPRAM_BASE];
    if (addr >= GPLB_IO_BASE && addr < GPLB_IO_BASE + GPLB_IO_SIZE)
        return c->io[addr - GPLB_IO_BASE];
    if (addr >= GPLB_ROM_FIXED)
        return c->rom[addr & 0x3FFF];
    /* ROM image layout: [0x0000:0x4000] fixed 16 KB bank, then 32 KB bank
     * windows from 0x4000. A CPU address in $4000-$BFFF therefore indexes the
     * image at addr + rom_bank directly -- subtracting the window base as well
     * reads 16 KB too low, which silently returns the fixed bank instead of
     * the selected one. That mistake made the firmware execute garbage and
     * wreck its stack at instruction ~238,000. */
    if (addr >= GPLB_ROM_WIN_LO)
        return c->rom[addr + c->rom_bank];
    return 0;   /* $0600-$3FFF is unmapped */
}

static inline void write8(gplb *c, uint32_t addr, uint8_t v)
{
    addr &= 0xFFFF;
    if (addr < GPLB_RAM_SIZE) { c->ram[addr] = v; return; }
    if (addr >= GPLB_DPRAM_BASE && addr < GPLB_DPRAM_BASE + GPLB_DPRAM_SIZE) {
        c->dpram[addr - GPLB_DPRAM_BASE] = v; return;
    }
    if (addr >= GPLB_IO_BASE && addr < GPLB_IO_BASE + GPLB_IO_SIZE) {
        c->io[addr - GPLB_IO_BASE] = v;
        io_write_side_effects(c, (uint16_t)(addr - GPLB_IO_BASE), v);
    }
}
#endif /* GPLB_FLAT_RAM */

/* ---- ALU -------------------------------------------------------------- */
static inline void do_ora(gplb *c, uint8_t v) { c->a |= v; c->nf = (c->a >> 7) & 1; c->zf = (c->a == 0); }
static inline void do_and(gplb *c, uint8_t v) { c->a &= v; c->nf = (c->a >> 7) & 1; c->zf = (c->a == 0); }
static inline void do_eor(gplb *c, uint8_t v) { c->a ^= v; c->nf = (c->a >> 7) & 1; c->zf = (c->a == 0); }

static inline void do_adc(gplb *c, uint8_t operand)
{
    uint8_t A = c->a;
    int32_t nv = (int32_t)A + operand + c->cf;

    if (c->df && (((A & 0x0F) + (operand & 0x0F) + c->cf) > 9)) nv += 6;

    c->vf = (uint8_t)(((~(A ^ operand) & (A ^ (uint8_t)nv)) >> 7) & 1);
    c->nf = (uint8_t)(((uint8_t)nv >> 7) & 1);

    if (c->df && (nv > 0x99)) nv += 0x60;

    c->zf = ((nv & 0xFF) == 0);
    c->cf = (nv > 255);
    c->a = (uint8_t)(nv & 0xFF);
}

static inline void do_sbc(gplb *c, uint8_t operand)
{
    uint8_t A = c->a;
    int32_t b = c->cf ? 0 : 1;
    int32_t nv = (int32_t)A - operand - b;

    if (c->df) {
        if (((A & 0x0F) - (operand & 0x0F) - b) < 0) nv -= 6;
        if (nv < 0) nv -= 0x60;
    }

    c->vf = (uint8_t)((((A ^ operand) & (A ^ (uint8_t)nv)) >> 7) & 1);
    c->nf = (uint8_t)(((uint8_t)nv >> 7) & 1);
    c->zf = ((nv & 0xFF) == 0);
    c->cf = (nv >= 0);
    c->a = (uint8_t)(nv & 0xFF);
}

static inline void do_cmp(gplb *c, uint8_t reg, uint8_t v)
{
    int32_t r = (int32_t)reg - v;
    c->cf = (r >= 0);
    c->zf = ((r & 0xFF) == 0);
    c->nf = (uint8_t)(((uint8_t)r >> 7) & 1);
}

static inline uint8_t do_asl(gplb *c, uint8_t v) { c->cf = (v >> 7) & 1; v = (uint8_t)(v << 1); c->nf = (v >> 7) & 1; c->zf = (v == 0); return v; }
static inline uint8_t do_lsr(gplb *c, uint8_t v) { c->cf = v & 1; v = (uint8_t)(v >> 1); c->nf = 0; c->zf = (v == 0); return v; }

static inline uint8_t do_rol(gplb *c, uint8_t v)
{
    uint8_t carry_in = c->cf;
    c->cf = (v >> 7) & 1;
    v = (uint8_t)((v << 1) | carry_in);
    c->nf = (v >> 7) & 1; c->zf = (v == 0);
    return v;
}

/* ROR -- standard 6502: the result is (v >> 1) with the old carry into bit 7,
 * CF takes v bit 0, N and Z come from the RESULT.
 *
 * The SPLB32 core this was derived from had the reference emulator's bug here
 * (Z computed from the old value, N from the old carry). That had to be
 * reproduced for the V3 because the ROM was timed against it; the GPLB is a
 * standard 6502 and Klaus Dormann's test fails at $283D without this. */
static inline uint8_t do_ror(gplb *c, uint8_t v)
{
    uint8_t carry_in = c->cf;
    uint8_t result = (uint8_t)((v >> 1) | (carry_in << 7));
    c->cf = v & 1;
    c->nf = (result >> 7) & 1;
    c->zf = (result == 0);
    return result;
}

/* ---- stack / interrupts ----------------------------------------------- */
static inline void push(gplb *c, uint8_t v)
{
    write8(c, (uint32_t)(SP_PAGE | c->sp), v);
    c->sp = (uint8_t)((c->sp - 1) & 0xFF);
}
static inline uint8_t pull(gplb *c)
{
    c->sp = (uint8_t)((c->sp + 1) & 0xFF);
    return read8(c, (uint32_t)(SP_PAGE | c->sp));
}

static void do_irq(gplb *c)
{
    push(c, (uint8_t)(c->pc >> 8));
    push(c, (uint8_t)(c->pc & 0xFF));
    push(c, get_ps(c));
    c->if_ = 1;
    c->pc = (uint16_t)(read8(c, 0xFFFE) | (read8(c, 0xFFFF) << 8));
}

/* ---- addressing modes -------------------------------------------------- */
static inline uint32_t am_zp(gplb *c)   { return read8(c, (uint32_t)c->pc++); }
static inline uint32_t am_zpx(gplb *c)  { return (uint32_t)((read8(c, (uint32_t)c->pc++) + c->x) & 0xFF); }
static inline uint32_t am_zpy(gplb *c)  { return (uint32_t)((read8(c, (uint32_t)c->pc++) + c->y) & 0xFF); }

static inline uint32_t am_abs(gplb *c)
{
    uint32_t a = read8(c, c->pc) | ((uint32_t)read8(c, (uint16_t)((c->pc + 1) & 0xFFFF)) << 8);
    c->pc = (uint16_t)((c->pc + 2) & 0xFFFF);
    return a;
}
static inline uint32_t am_absx(gplb *c)
{
    uint32_t base = am_abs(c);
    uint32_t addr = (base + c->x) & 0xFFFF;
    c->page_crossed = ((base ^ addr) > 255u);
    return addr;
}
static inline uint32_t am_absy(gplb *c)
{
    uint32_t base = am_abs(c);
    uint32_t addr = (base + c->y) & 0xFFFF;
    c->page_crossed = ((base ^ addr) > 255u);
    return addr;
}

static inline uint32_t am_indx(gplb *c)
{
    uint32_t z = (uint32_t)((read8(c, (uint32_t)c->pc++) + c->x) & 0xFF);
    return read8(c, z) | ((uint32_t)read8(c, (z + 1) & 0xFF) << 8);
}
static inline uint32_t am_indy(gplb *c)
{
    uint32_t z = read8(c, (uint32_t)c->pc++);
    uint32_t base = read8(c, z) | ((uint32_t)read8(c, (z + 1) & 0xFF) << 8);
    uint32_t addr = (base + c->y) & 0xFFFF;
    c->page_crossed = ((base ^ addr) > 255u);
    return addr;
}

static inline uint32_t branch(gplb *c, int taken)
{
    c->branch_taken = (uint8_t)taken;
    if (taken) {
        int8_t rel = (int8_t)read8(c, c->pc);
        uint16_t prev = (uint16_t)((c->pc + 1) & 0xFFFF);
        c->pc = (uint16_t)((prev + (int16_t)rel) & 0xFFFF);
        c->page_crossed = ((c->pc ^ prev) > 255u);
        return 3u + (c->page_crossed ? 1u : 0u);
    }
    c->pc = (uint16_t)((c->pc + 1) & 0xFFFF);
    return 2u;
}
/* ---- cycle table ------------------------------------------------------ */
/* 1 where a page crossing adds a cycle: the indexed read modes only. Store
 * and read-modify-write forms have fixed counts on the 6502. */
static const uint8_t cyc_page[256] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,1,0,0,0,0,1,0,0,1,0,0,1,1,1,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,
};

static const uint8_t cyc_base[256] = {
   7, 6, 4, 4, 4, 3, 5, 4, 3, 2, 2, 4, 4, 4, 6, 4,
   2, 5, 4, 4, 4, 4, 6, 4, 2, 4, 4, 4, 4, 4, 7, 4,
   6, 6, 4, 4, 3, 3, 5, 4, 4, 2, 2, 4, 4, 4, 6, 4,
   2, 5, 4, 4, 4, 4, 6, 4, 2, 4, 4, 4, 4, 4, 7, 4,
   6, 6, 4, 4, 4, 3, 5, 4, 3, 2, 2, 4, 3, 4, 6, 4,
   2, 5, 4, 4, 4, 4, 6, 4, 2, 4, 4, 4, 4, 4, 7, 4,
   6, 6, 4, 4, 4, 3, 5, 4, 4, 2, 2, 4, 5, 4, 6, 4,
   2, 5, 4, 4, 4, 4, 6, 4, 2, 4, 4, 4, 4, 4, 7, 4,
   4, 6, 4, 4, 3, 3, 3, 4, 2, 4, 2, 4, 4, 4, 4, 4,
   2, 6, 4, 4, 4, 4, 4, 4, 2, 5, 2, 4, 4, 5, 4, 4,
   2, 6, 2, 4, 3, 3, 3, 4, 2, 2, 2, 4, 4, 4, 4, 4,
   2, 5, 4, 4, 4, 4, 4, 4, 2, 4, 2, 4, 4, 4, 4, 4,
   2, 6, 4, 4, 3, 3, 5, 4, 2, 2, 2, 4, 4, 4, 6, 4,
   2, 5, 4, 4, 4, 4, 6, 4, 2, 4, 4, 4, 4, 4, 7, 4,
   2, 6, 4, 4, 3, 3, 5, 4, 2, 2, 2, 4, 4, 4, 6, 4,
   2, 5, 4, 4, 4, 4, 6, 4, 2, 4, 4, 4, 4, 4, 7, 4,
};

/* ---- opcode dispatch ---------------------------------------------------
 * Standard 6502 layout plus the Sunplus SFR/bank behaviour supplied by the
 * bus. Cycle counts are the reference core's raw values; the clock divider is
 * applied by the caller.
 * ---------------------------------------------------------------------- */
static void exec_op_sem(gplb *c, uint8_t op)
{
    uint32_t ad;
    c->page_crossed = 0;
    c->branch_taken = 0;

    switch (op) {
    /* ---- BRK / stack ---- */
    case 0x00:
        /* BRK is two bytes: the opcode and a padding byte. The address pushed
         * is that of the padding byte's successor, so PC must advance once more
         * before the interrupt pushes it. Klaus Dormann's test checks this at
         * $37D5/$37DC by reading the pushed address back off the stack. */
        c->pc = (uint16_t)((c->pc + 1) & 0xFFFF);
        c->bf = 1;
        do_irq(c);
        return;
    case 0x40: c->sp = (uint8_t)((c->sp + 1) & 0xFF); set_ps(c, read8(c, SP_PAGE | c->sp));
               c->sp = (uint8_t)((c->sp + 1) & 0xFF); c->pc = read8(c, SP_PAGE | c->sp);
               c->sp = (uint8_t)((c->sp + 1) & 0xFF);
               c->pc |= (uint16_t)(read8(c, SP_PAGE | c->sp) << 8); return;
    case 0x60: c->sp = (uint8_t)((c->sp + 1) & 0xFF); c->pc = read8(c, SP_PAGE | c->sp);
               c->sp = (uint8_t)((c->sp + 1) & 0xFF);
               c->pc |= (uint32_t)(read8(c, SP_PAGE | c->sp) << 8);
               c->pc = c->pc + 1;          /* deliberately unmasked, as reference */
               return;
    case 0x08: push(c, get_ps(c)); return;
    case 0x28: set_ps(c, pull(c)); return;
    case 0x48: push(c, c->a); return;
    case 0x68: c->a = pull(c); c->nf = (c->a >> 7) & 1; c->zf = (c->a == 0); return;

    /* ---- ORA ---- */
    case 0x09: do_ora(c, read8(c, (uint32_t)c->pc++)); return;
    case 0x05: do_ora(c, read8(c, am_zp(c))); return;
    case 0x15: do_ora(c, read8(c, am_zpx(c))); return;
    case 0x0D: do_ora(c, read8(c, am_abs(c))); return;
    case 0x1D: do_ora(c, read8(c, am_absx(c))); return;
    case 0x19: do_ora(c, read8(c, am_absy(c))); return;
    case 0x01: do_ora(c, read8(c, am_indx(c))); return;
    case 0x11: do_ora(c, read8(c, am_indy(c))); return;

    /* ---- AND ---- */
    case 0x29: do_and(c, read8(c, (uint32_t)c->pc++)); return;
    case 0x25: do_and(c, read8(c, am_zp(c))); return;
    case 0x35: do_and(c, read8(c, am_zpx(c))); return;
    case 0x2D: do_and(c, read8(c, am_abs(c))); return;
    case 0x3D: do_and(c, read8(c, am_absx(c))); return;
    case 0x39: do_and(c, read8(c, am_absy(c))); return;
    case 0x21: do_and(c, read8(c, am_indx(c))); return;
    case 0x31: do_and(c, read8(c, am_indy(c))); return;

    /* ---- EOR ---- */
    case 0x49: do_eor(c, read8(c, (uint32_t)c->pc++)); return;
    case 0x45: do_eor(c, read8(c, am_zp(c))); return;
    case 0x55: do_eor(c, read8(c, am_zpx(c))); return;
    case 0x4D: do_eor(c, read8(c, am_abs(c))); return;
    case 0x5D: do_eor(c, read8(c, am_absx(c))); return;
    case 0x59: do_eor(c, read8(c, am_absy(c))); return;
    case 0x41: do_eor(c, read8(c, am_indx(c))); return;
    case 0x51: do_eor(c, read8(c, am_indy(c))); return;

    /* ---- ADC ---- */
    case 0x69: do_adc(c, read8(c, (uint32_t)c->pc++)); return;
    case 0x65: do_adc(c, read8(c, am_zp(c))); return;
    case 0x75: do_adc(c, read8(c, am_zpx(c))); return;
    case 0x6D: do_adc(c, read8(c, am_abs(c))); return;
    case 0x7D: do_adc(c, read8(c, am_absx(c))); return;
    case 0x79: do_adc(c, read8(c, am_absy(c))); return;
    case 0x61: do_adc(c, read8(c, am_indx(c))); return;
    case 0x71: do_adc(c, read8(c, am_indy(c))); return;

    /* ---- SBC ---- */
    case 0xE9: do_sbc(c, read8(c, (uint32_t)c->pc++)); return;
    case 0xE5: do_sbc(c, read8(c, am_zp(c))); return;
    case 0xF5: do_sbc(c, read8(c, am_zpx(c))); return;
    case 0xED: do_sbc(c, read8(c, am_abs(c))); return;
    case 0xFD: do_sbc(c, read8(c, am_absx(c))); return;
    case 0xF9: do_sbc(c, read8(c, am_absy(c))); return;
    case 0xE1: do_sbc(c, read8(c, am_indx(c))); return;
    case 0xF1: do_sbc(c, read8(c, am_indy(c))); return;

    /* ---- CMP / CPX / CPY ---- */
    case 0xC9: do_cmp(c, c->a, read8(c, (uint32_t)c->pc++)); return;
    case 0xC5: do_cmp(c, c->a, read8(c, am_zp(c))); return;
    case 0xD5: do_cmp(c, c->a, read8(c, am_zpx(c))); return;
    case 0xCD: do_cmp(c, c->a, read8(c, am_abs(c))); return;
    case 0xDD: do_cmp(c, c->a, read8(c, am_absx(c))); return;
    case 0xD9: do_cmp(c, c->a, read8(c, am_absy(c))); return;
    case 0xC1: do_cmp(c, c->a, read8(c, am_indx(c))); return;
    case 0xD1: do_cmp(c, c->a, read8(c, am_indy(c))); return;
    case 0xE0: do_cmp(c, c->x, read8(c, (uint32_t)c->pc++)); return;
    case 0xE4: do_cmp(c, c->x, read8(c, am_zp(c))); return;
    case 0xEC: do_cmp(c, c->x, read8(c, am_abs(c))); return;
    case 0xC0: do_cmp(c, c->y, read8(c, (uint32_t)c->pc++)); return;
    case 0xC4: do_cmp(c, c->y, read8(c, am_zp(c))); return;
    case 0xCC: do_cmp(c, c->y, read8(c, am_abs(c))); return;

    /* ---- LDA ---- */
    case 0xA9: c->a = read8(c, (uint32_t)c->pc++); goto load_tail;
    case 0xA5: c->a = read8(c, am_zp(c));         goto load_tail;
    case 0xB5: c->a = read8(c, am_zpx(c));        goto load_tail;
    case 0xAD: c->a = read8(c, am_abs(c));        goto load_tail;
    case 0xBD: c->a = read8(c, am_absx(c));       goto load_tail;
    case 0xB9: c->a = read8(c, am_absy(c));       goto load_tail;
    case 0xA1: c->a = read8(c, am_indx(c));       goto load_tail;
    case 0xB1: c->a = read8(c, am_indy(c));       goto load_tail;

    /* ---- LDX ---- */
    case 0xA2: c->x = read8(c, (uint32_t)c->pc++); goto load_tail;
    case 0xA6: c->x = read8(c, am_zp(c));          goto load_tail;
    case 0xB6: c->x = read8(c, am_zpy(c));         goto load_tail;
    case 0xAE: c->x = read8(c, am_abs(c));         goto load_tail;
    case 0xBE: c->x = read8(c, am_absy(c));        goto load_tail;

    /* ---- LDY ---- */
    case 0xA0: c->y = read8(c, (uint32_t)c->pc++); goto load_tail;
    case 0xA4: c->y = read8(c, am_zp(c));          goto load_tail;
    case 0xB4: c->y = read8(c, am_zpx(c));         goto load_tail;
    case 0xAC: c->y = read8(c, am_abs(c));         goto load_tail;
    case 0xBC: c->y = read8(c, am_absx(c));        goto load_tail;

load_tail:
    /* which register was loaded is determined by the opcode */
    switch (op) {
    case 0xA9: case 0xA5: case 0xB5: case 0xAD: case 0xBD:
    case 0xB9: case 0xA1: case 0xB1:
        c->nf = (c->a >> 7) & 1; c->zf = (c->a == 0); break;
    case 0xA2: case 0xA6: case 0xB6: case 0xAE: case 0xBE:
        c->nf = (c->x >> 7) & 1; c->zf = (c->x == 0); break;
    default:
        c->nf = (c->y >> 7) & 1; c->zf = (c->y == 0); break;
    }
    return;

    /* ---- STA / STX / STY ---- */
    case 0x85: write8(c, am_zp(c), c->a); return;
    case 0x95: write8(c, am_zpx(c), c->a); return;
    case 0x8D: write8(c, am_abs(c), c->a); return;
    case 0x9D: write8(c, am_absx(c), c->a); return;
    case 0x99: write8(c, am_absy(c), c->a); return;
    case 0x81: write8(c, am_indx(c), c->a); return;
    case 0x91: write8(c, am_indy(c), c->a); return;
    case 0x86: write8(c, am_zp(c), c->x); return;
    case 0x96: write8(c, am_zpy(c), c->x); return;
    case 0x8E: write8(c, am_abs(c), c->x); return;
    case 0x84: write8(c, am_zp(c), c->y); return;
    case 0x94: write8(c, am_zpx(c), c->y); return;
    case 0x8C: write8(c, am_abs(c), c->y); return;

    /* ---- BIT ---- */
    case 0x24: ad = am_zp(c);
               { uint8_t v = read8(c, ad); c->zf = ((c->a & v) == 0);
                 c->nf = (v >> 7) & 1; c->vf = (v >> 6) & 1; } return;
    case 0x2C: ad = am_abs(c);
               { uint8_t v = read8(c, ad); c->zf = ((c->a & v) == 0);
                 c->nf = (v >> 7) & 1; c->vf = (v >> 6) & 1; } return;

    /* ---- shifts: ASL/LSR/ROL/ROR ---- */
    case 0x0A: c->a = do_asl(c, c->a); return;
    case 0x06: ad = am_zp(c); write8(c, ad, do_asl(c, read8(c, ad))); return;
    case 0x16: ad = am_zpx(c); write8(c, ad, do_asl(c, read8(c, ad))); return;
    case 0x0E: ad = am_abs(c); write8(c, ad, do_asl(c, read8(c, ad))); return;
    case 0x1E: ad = am_absx(c); write8(c, ad, do_asl(c, read8(c, ad))); return;

    case 0x4A: c->a = do_lsr(c, c->a); return;
    case 0x46: ad = am_zp(c); write8(c, ad, do_lsr(c, read8(c, ad))); return;
    case 0x56: ad = am_zpx(c); write8(c, ad, do_lsr(c, read8(c, ad))); return;
    case 0x4E: ad = am_abs(c); write8(c, ad, do_lsr(c, read8(c, ad))); return;
    case 0x5E: ad = am_absx(c); write8(c, ad, do_lsr(c, read8(c, ad))); return;

    case 0x2A: c->a = do_rol(c, c->a); return;
    case 0x26: ad = am_zp(c); write8(c, ad, do_rol(c, read8(c, ad))); return;
    case 0x36: ad = am_zpx(c); write8(c, ad, do_rol(c, read8(c, ad))); return;
    case 0x2E: ad = am_abs(c); write8(c, ad, do_rol(c, read8(c, ad))); return;
    case 0x3E: ad = am_absx(c); write8(c, ad, do_rol(c, read8(c, ad))); return;

    case 0x6A: c->a = do_ror(c, c->a); return;
    case 0x66: ad = am_zp(c); write8(c, ad, do_ror(c, read8(c, ad))); return;
    case 0x76:
        /* Standard 6502: ROR zp,X advances PC by 1 like every other zp,X.
         * The SPLB32 core this came from reproduced a reference-emulator bug
         * that advanced by 2; that was right for the V3 and is wrong here. */
        ad = am_zpx(c);
        write8(c, ad, do_ror(c, read8(c, ad)));
        return;
    case 0x6E: ad = am_abs(c); write8(c, ad, do_ror(c, read8(c, ad))); return;
    case 0x7E: ad = am_absx(c); write8(c, ad, do_ror(c, read8(c, ad))); return;

    /* ---- INC / DEC ---- */
    case 0xE6: ad = am_zp(c);  { uint8_t v = (uint8_t)(read8(c, ad) + 1);
                 write8(c, ad, v); c->nf = (v >> 7) & 1; c->zf = (v == 0); } return;
    case 0xF6: ad = am_zpx(c); { uint8_t v = (uint8_t)(read8(c, ad) + 1);
                 write8(c, ad, v); c->nf = (v >> 7) & 1; c->zf = (v == 0); } return;
    case 0xEE: ad = am_abs(c); { uint8_t v = (uint8_t)(read8(c, ad) + 1);
                 write8(c, ad, v); c->nf = (v >> 7) & 1; c->zf = (v == 0); } return;
    case 0xFE: ad = am_absx(c);{ uint8_t v = (uint8_t)(read8(c, ad) + 1);
                 write8(c, ad, v); c->nf = (v >> 7) & 1; c->zf = (v == 0); } return;
    case 0xC6: ad = am_zp(c);  { uint8_t v = (uint8_t)(read8(c, ad) - 1);
                 write8(c, ad, v); c->nf = (v >> 7) & 1; c->zf = (v == 0); } return;
    case 0xD6: ad = am_zpx(c); { uint8_t v = (uint8_t)(read8(c, ad) - 1);
                 write8(c, ad, v); c->nf = (v >> 7) & 1; c->zf = (v == 0); } return;
    case 0xCE: ad = am_abs(c); { uint8_t v = (uint8_t)(read8(c, ad) - 1);
                 write8(c, ad, v); c->nf = (v >> 7) & 1; c->zf = (v == 0); } return;
    case 0xDE: ad = am_absx(c);{ uint8_t v = (uint8_t)(read8(c, ad) - 1);
                 write8(c, ad, v); c->nf = (v >> 7) & 1; c->zf = (v == 0); } return;

    /* ---- register ops ---- */
    case 0xE8: c->x++; c->nf = (c->x >> 7) & 1; c->zf = (c->x == 0); return;
    case 0xC8: c->y++; c->nf = (c->y >> 7) & 1; c->zf = (c->y == 0); return;
    case 0xCA: c->x--; c->nf = (c->x >> 7) & 1; c->zf = (c->x == 0); return;
    case 0x88: c->y--; c->nf = (c->y >> 7) & 1; c->zf = (c->y == 0); return;
    case 0xAA: c->x = c->a; c->nf = (c->x >> 7) & 1; c->zf = (c->x == 0); return;
    case 0xA8: c->y = c->a; c->nf = (c->y >> 7) & 1; c->zf = (c->y == 0); return;
    case 0x8A: c->a = c->x; c->nf = (c->a >> 7) & 1; c->zf = (c->a == 0); return;
    case 0x98: c->a = c->y; c->nf = (c->a >> 7) & 1; c->zf = (c->a == 0); return;
    case 0xBA: c->x = c->sp; c->nf = (c->x >> 7) & 1; c->zf = (c->x == 0); return;
    case 0x9A: c->sp = c->x; return;
    case 0xEA: return;

    /* ---- jumps ---- */
    case 0x4C: c->pc = (uint16_t)(read8(c, c->pc) | (read8(c, (uint16_t)((c->pc + 1) & 0xFFFF)) << 8)); return;
    case 0x6C: { uint16_t ptr = (uint16_t)(read8(c, c->pc) | (read8(c, (uint16_t)((c->pc + 1) & 0xFFFF)) << 8));
                 uint16_t lo = read8(c, ptr);
                 /* reference reads the high byte at ptr+1 with no 6502 page wrap */
                 uint16_t hi = read8(c, (uint16_t)((ptr + 1) & 0xFFFF));
                 c->pc = (uint16_t)(lo | (hi << 8)); } return;
    case 0x20: { uint16_t pc = (uint16_t)((c->pc + 1) & 0xFFFF);
                 push(c, (uint8_t)(pc >> 8));
                 push(c, (uint8_t)(pc & 0xFF));
                 c->pc = (uint16_t)(read8(c, c->pc) | (read8(c, pc) << 8)); } return;

    /* ---- branches ---- */
    case 0x10: branch(c, !c->nf); return;
    case 0x30: branch(c, c->nf); return;
    case 0x50: branch(c, !c->vf); return;
    case 0x70: branch(c, c->vf); return;
    case 0x90: branch(c, !c->cf); return;
    case 0xB0: branch(c, c->cf); return;
    case 0xD0: branch(c, !c->zf); return;
    case 0xF0: branch(c, c->zf); return;

    /* ---- flag ops ---- */
    case 0x18: c->cf = 0; return;
    case 0x38: c->cf = 1; return;
    case 0x58: c->if_ = 0; return;
    case 0x78: c->if_ = 1; return;
    case 0xB8: c->vf = 0; return;
    case 0xD8: c->df = 0; return;
    case 0xF8: c->df = 1; return;

    default: return;   /* illegal: reference returns its table entry */
    }
}

/* ---- core step -------------------------------------------------------- */
uint32_t gplb_step(gplb *c)
{
    uint8_t op = read8(c, c->pc);
    c->pc = (uint16_t)((c->pc + 1) & 0xFFFF);
    c->instrs++;
    c->page_crossed = 0;
    c->branch_taken = 0;

    exec_op_sem(c, op);

    uint32_t raw = cyc_base[op];

    /* Standard 6502 timing: a branch costs +1 when taken and +1 more if it
     * crosses a page; the indexed read modes cost +1 on a page cross. The
     * table's values are the base counts. */
    if (op == 0x10 || op == 0x30 || op == 0x50 || op == 0x70 ||
        op == 0x90 || op == 0xB0 || op == 0xD0 || op == 0xF0) {
        raw = c->branch_taken ? (2u + c->page_crossed) : 2u;
    } else if (c->page_crossed && cyc_page[op]) {
        raw += 1;
    }

    c->cycles += raw;

    if (!c->if_ && (c->ireq & c->int_enbl))
        do_irq(c);

    return raw;
}

uint64_t gplb_run_cycles(gplb *c, uint64_t n)
{
    uint64_t t = c->cycles + n;
    while (c->cycles < t) gplb_step(c);
    return c->cycles;
}

uint64_t gplb_run_instrs(gplb *c, uint64_t n)
{
    uint64_t t = c->instrs + n;
    while (c->instrs < t) gplb_step(c);
    return c->instrs;
}

/* ---- lifecycle -------------------------------------------------------- */
gplb *gplb_new(const uint8_t *rom, size_t len)
{
    if (!rom || len < 0x10000) return NULL;
    gplb *c = (gplb *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->rom = rom; c->rom_len = len;
    gplb_reset(c);
    return c;
}

void gplb_free(gplb *c) { free(c); }

void gplb_reset(gplb *c)
{
    memset(c->ram, 0, sizeof(c->ram));
    memset(c->dpram, 0, sizeof(c->dpram));
    memset(c->io, 0, sizeof(c->io));
    c->rom_bank = 0;
    c->a = c->x = c->y = 0;
    c->sp = 0xFF;
    set_ps(c, 0x24);
    c->ireq = c->int_enbl = 0;
    c->instrs = c->cycles = 0;

    /* vectors live in the fixed bank */
    c->pc = (uint16_t)(read8(c, 0xFFFC) | (read8(c, 0xFFFD) << 8));
}

void gplb_snapshot(const gplb *c, gplb_state *s)
{
    s->pc = c->pc;
    s->a = c->a; s->x = c->x; s->y = c->y; s->sp = c->sp;
    s->p = get_ps(c);
    s->instrs = c->instrs;
    s->cycles = c->cycles;
    s->rom_bank = c->rom_bank;
    memcpy(s->ram, c->ram, sizeof(s->ram));
    memcpy(s->dpram, c->dpram, sizeof(s->dpram));
}

const uint8_t *gplb_dpram(const gplb *c) { return c->dpram; }
uint8_t *gplb_io_regs(gplb *c) { return c->io; }
uint8_t gplb_peek(gplb *c, uint32_t addr) { return read8(c, addr); }

/* Test hook: jump the core to an address without touching memory. */
void gplb_force_pc(gplb *c, uint16_t pc) { c->pc = pc; }
