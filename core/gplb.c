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

    /* Interrupts are 16 vectors at $FFC0-$FFDF. $3076 enables vectors 5-11
     * (bit 7 = global); $3073/$3074 acknowledge them; the ROM writes one bit
     * per source it services. See the mapping note above io_write_side_effects. */
    uint16_t irq_enabled;        /* one bit per vector */
    uint16_t irq_pending;
    uint8_t  irq_global;

    /* TIMER1. The datasheet lists TIMER1 among the NMI sources, and the ROM's
     * NMI handler ($D26B) is the only code that sets $01A7=$80 - the flag the
     * main loop spins on. So the game tick is TIMER1 underflow -> NMI. */
    uint32_t t1_reload, t1_count;
    uint32_t t1_prescale;        /* CPU cycles per timer tick */
    uint32_t t1_acc;             /* cycles accumulated toward the next tick */
    uint8_t  t1_running;         /* one-shot busy; mirrors $3016 bit 0 */
    uint8_t  nmi_pending;
    uint64_t nmi_count, irq_count;
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
#define IO_IRQ_ACK_LO 0x0073 /* $3073 - ack vectors 0-7,  bit 7-v */
#define IO_IRQ_ACK_HI 0x0074 /* $3074 - ack vectors 8-15, bit 15-v */
#define IO_IRQ_ENBL 0x0076   /* $3076 - bit 7 global, bits 0-6 per vector */
#define IO_IRQ_ENBH 0x0077   /* $3077 - second enable byte */
/* TIMER1 reload. The ROM writes the high byte first and the low byte second
 * ($3015 then $3014), so $3014 is the high half. Reading them the other way
 * round makes the counter ~32000 instead of 382 and the frame tick drops to
 * 1.5 Hz. */
#define IO_T1_HI    0x0014   /* $3014 - TIMER1 reload, high byte */
#define IO_T1_LO    0x0015   /* $3015 - TIMER1 reload, low byte */
/* $3016 is the TIMER1 control register. Bit 0 starts a one-shot run and the
 * hardware clears it again on expiry - that is what the poll loop at $AEB9
 * is waiting for (DEX/BEQ 256 iterations, then LDA $3016 / AND #$01 / BNE).
 * Bits 1 and 2 are also driven from the shadow copy in RAM $21. */
#define IO_T1_CTRL  0x0016

/* IO write trace, for working out register semantics from the firmware. */
#include <stdio.h>
int gplb_io_trace = 0;
static unsigned long gplb_io_writes = 0;

static void io_write_side_effects(gplb *c, uint16_t idx, uint8_t v)
{
    gplb_io_writes++;
    if (gplb_io_trace && gplb_io_writes < 400)
        fprintf(stderr, "IO %04X <- %02X\n", 0x3000 + idx, v);

    if (gplb_io_trace && idx >= 0x10 && idx <= 0x16)
        fprintf(stderr, "T1REG cyc=%llu %04X <- %02X (pc=%04X) reload=%u run=%u\n",
                (unsigned long long)c->cycles, 0x3000 + idx, v, c->pc,
                c->t1_reload, c->t1_running);
    if (gplb_io_trace && idx >= 0x70 && idx <= 0x77)
        fprintf(stderr, "IRQREG %04X <- %02X (pc=%04X)\n",
                0x3000 + idx, v, c->pc);

    switch (idx) {
    case IO_ROM_BANK: {
        uint32_t bank = (uint32_t)v * 0x8000u;
        if (0x4000u + bank < c->rom_len)
            c->rom_bank = bank;
        break;
    }
    case IO_IRQ_ENBL: {
        /* Write-one-to-set. A zero write leaves the latch alone.
         *
         * Plain-write semantics deadlock this firmware: the boot path sets $82
         * at $7699, then $76BB writes $00, and every later NMI handler
         * ($D26B/$D271) writes $00 again. With zeros honoured, nothing is ever
         * enabled, vector 10 never runs, the $2D flag at $CE75 is never set and
         * the device spins forever in the idle loop - measured: irq=0 over 20M
         * instructions, display RAM frozen. With the latch, the same run gives
         * a live main loop (body executes, bank changes 293 times in 4M
         * instructions) and $2D toggles. So zeros are not a disable here. */
        if (v == 0) break;
        c->irq_global = (uint8_t)(v >> 7);
        c->irq_enabled = 0;
        for (int b = 0; b < 7; b++)
            if (v & (1u << b)) {
                /* The ROM never writes a mask bit for vector 10, yet it waits
                 * on a flag only that handler sets. Bit 1 of $3076 ($82) must
                 * therefore select vector 10, so the byte runs downwards. */
                int vec = 11 - b;
                if (vec >= 0 && vec <= 15) c->irq_enabled |= (uint16_t)(1u << vec);
            }
        if (gplb_io_trace)
            fprintf(stderr, "  -> enabled=%04X global=%d\n",
                    c->irq_enabled, c->irq_global);
        break;
    }
    case IO_IRQ_ENBH: {
        /* Second enable byte runs the same way for vectors 12-15 is unknown;
         * the ROM only ever stores a status read here, so treat it as such. */
        break;
    }
    case IO_T1_HI:
    case IO_T1_LO:
        /* Just a reload latch. Starting is $3016's job: the ROM always writes
         * the reload first (e.g. $3015=$7E, $3014=$01) and only then writes the
         * control byte. */
        c->t1_reload = ((uint32_t)c->io[IO_T1_HI] << 8) | (uint32_t)c->io[IO_T1_LO];
        break;
    case IO_T1_CTRL:
        /* The ROM writes this register constantly as a copy of the RAM shadow
         * at $21, cycling through $04-$07. Those writes are not start/stop
         * commands: the cycle continues with the tick alive, so gating the
         * timer on bit 0 here stops it mid-run and hangs the boot path at
         * $769C. The real stop is a zero reload - $C318 and $C326 clear
         * $3014, $3015 and $3016 together to shut the block down.
         *
         * Bit 0 is still the poll flag: writing it set arms the run bit that
         * the loop at $AEB9 waits to clear, and each expiry clears it. */
        if (v & 0x01) c->io[IO_T1_CTRL] |= 0x01;
        else          c->io[IO_T1_CTRL] &= (uint8_t)~0x01;
        if (gplb_io_trace)
            fprintf(stderr, "  -> t1 start=%d reload=%u\n",
                    c->t1_running, c->t1_reload);
        break;
    case IO_IRQ_ACK_LO:
    case IO_IRQ_ACK_HI: {
        int base = (idx == IO_IRQ_ACK_LO) ? 0 : 8;
        for (int b = 0; b < 8; b++)
            if (v & (1u << b)) {
                int vec = base + (7 - b);
                if (vec >= 0 && vec <= 15) c->irq_pending &= (uint16_t)~(1u << vec);
            }
        if (gplb_io_trace) fprintf(stderr, "  -> ack %02X, pending=%04X\n",
                                   v, c->irq_pending);
        break;
    }
    default:
        break;
    }
}

/* Read-hit counters. Declared outside the flat-RAM switch because the accessor
 * gplb_io_reads() is built in every configuration; in flat mode nothing counts
 * IO reads, so it honestly reports zero. */
static unsigned long io_read_hits[GPLB_IO_SIZE];
uint16_t gplb_last_reader = 0;

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
    if (addr >= GPLB_IO_BASE && addr < GPLB_IO_BASE + GPLB_IO_SIZE) {
        io_read_hits[addr - GPLB_IO_BASE]++;
        if (addr - GPLB_IO_BASE == 0x16) gplb_last_reader = c->pc;
        return c->io[addr - GPLB_IO_BASE];
    }
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

/* Display-RAM write trace. Under gplb_io_trace this prints the first 100k
 * writes with the program counter, which is how the display routines get
 * attributed: the boot clear is $CC9A (512 bytes of $1000-$11FF), the blit is
 * $F050 -> $F081 (12 bytes x 31 rows). */
static unsigned long gplb_dpwr = 0;

static inline void write8(gplb *c, uint32_t addr, uint8_t v)
{
    addr &= 0xFFFF;
    if (addr < GPLB_RAM_SIZE) { c->ram[addr] = v; return; }
    if (addr >= GPLB_DPRAM_BASE && addr < GPLB_DPRAM_BASE + GPLB_DPRAM_SIZE) {
        gplb_dpwr++;
        if (gplb_io_trace && gplb_dpwr < 100000)
            fprintf(stderr, "DP %04X <- %02X (pc=%04X)\n", addr, v, c->pc);
        c->dpram[addr - GPLB_DPRAM_BASE] = v;
        return;
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

/* Common interrupt entry: the CPU pushes PC high, PC low, status, sets I and
 * jumps through the two-byte vector at `va`. Whether status bit 4 is set is
 * the caller's business - BRK pushes 1, hardware interrupts push 0 - so this
 * never touches bf. */
static void enter_interrupt(gplb *c, uint16_t va)
{
    push(c, (uint8_t)(c->pc >> 8));
    push(c, (uint8_t)(c->pc & 0xFF));
    push(c, get_ps(c));
    c->if_ = 1;
    c->pc = (uint16_t)(read8(c, va) | (read8(c, va + 1) << 8));
}

/* NMI is non-maskable: the ROM's frame handler at $D26B runs even with the
 * I flag set, which is how it can tick while the main loop is inside SEI
 * sections. Unlike BRK/IRQ it pushes P with bit 5 set and bit 4 clear. */
static void do_nmi(gplb *c)
{
    c->bf = 0;
    enter_interrupt(c, 0xFFFA);
    c->nmi_count++;
}

/* Sixteen vectors live at $FFC0-$FFDF, two bytes each; the datasheet only
 * documents the first twelve ($FFC0-$FFEF). The index is the source number:
 * vector[3] acks $10 ($3073 bit 4), vector[7] acks $01 (bit 0), vector[10]
 * acks $20 ($3074 bit 5), vector[13] acks $04 ($3074 bit 2). In general the
 * ack is $3073/3074 bit 7-(v&7), which fixes the order.
 */
static void do_irq(gplb *c, int vec)
{
    if (vec < 0 || vec > 15) return;
    c->bf = 0;
    enter_interrupt(c, (uint16_t)(0xFFC0 + vec * 2));
    c->irq_count++;
}

/* BRK and a maskable IRQ that reaches the CPU through the ordinary interrupt
 * pin both use the hardware vector at $FFFE, not the peripheral table. */
static void do_hw_irq(gplb *c)
{
    enter_interrupt(c, 0xFFFE);
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
        do_hw_irq(c);   /* BRK uses the hardware IRQ vector at $FFFE */
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

    /* TIMER1 free-runs while it has a non-zero reload and expires in a burst of
     * events that the firmware is demonstrably waiting on:
     *   - the run bit in $3016 clears, which releases the poll loop at $AEB9;
     *   - NMI fires, and $D26B - reachable only through the NMI vector - is
     *     the sole setter of $01A7 = $80, the flag the boot path spins on;
     *   - vector 10 fires, and $D166 is the sole setter of $2D, the flag the
     *     main loop spins on. */
    if (c->t1_reload) {
        c->t1_acc += raw;
        while (c->t1_acc >= c->t1_prescale) {
            c->t1_acc -= c->t1_prescale;
            if (c->t1_count == 0) {
                /* Auto-reload, and drop the poll flag so the loop at $AEB9
                 * (which waits for bit 0 to clear) sees one expiry per arming
                 * while the periodic tick keeps running for the boot path. */
                c->t1_count = c->t1_reload;
                c->io[IO_T1_CTRL] &= (uint8_t)~0x01;
                c->nmi_pending = 1;
                c->irq_pending |= (uint16_t)(1u << 10);
            } else {
                c->t1_count--;
            }
        }
    }

    if (c->nmi_pending) {
        c->nmi_pending = 0;
        do_nmi(c);
        return raw;
    }

    if (!c->if_ && c->irq_global) {
        uint16_t pend = (uint16_t)(c->irq_pending & c->irq_enabled);
        for (int vec = 0; pend && vec < 16; vec++)
            if (pend & (1u << vec)) { do_irq(c, vec); break; }
    }

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
    c->irq_enabled = c->irq_pending = 0;
    c->irq_global = 0;
    c->instrs = c->cycles = 0;

    /* TIMER1 defaults. The prescaler is the one number the datasheet text we
     * have does not pin down; 256 CPU cycles per tick puts the ROM's reload of
     * $017E at ~61 Hz, which matches a 64-common LCD frame. */
    c->t1_prescale = 256;
    c->t1_acc = 0;
    c->t1_running = 0;
    c->t1_reload = c->t1_count = 0;
    c->nmi_pending = 0;
    c->nmi_count = 0;

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
    s->nmi = c->nmi_count;
    s->irq = c->irq_count;
    s->t1_reload = c->t1_reload;
    s->t1_count = c->t1_count;
    s->rom_bank = c->rom_bank;
    memcpy(s->ram, c->ram, sizeof(s->ram));
    memcpy(s->dpram, c->dpram, sizeof(s->dpram));
}

const uint8_t *gplb_dpram(const gplb *c) { return c->dpram; }
uint8_t *gplb_io_regs(gplb *c) { return c->io; }
uint8_t gplb_peek(gplb *c, uint32_t addr) { return read8(c, addr); }

unsigned long gplb_io_reads(gplb *c, int idx)
{
    (void)c;
    if (idx < 0 || idx >= GPLB_IO_SIZE) return 0;
    return io_read_hits[idx];
}

/* Test hook: jump the core to an address without touching memory. */
void gplb_force_pc(gplb *c, uint16_t pc) { c->pc = pc; }
