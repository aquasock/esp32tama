/*
 * e0c6200.h - Epson E0C6200 4-bit CPU core
 *
 * The microcontroller in the original Tamagotchi (P1, P2), Angel, Ocean,
 * Morino, Mothra, Genjintch, Yasashii and the Digimon virtual pets.
 *
 * A 4-bit machine: 4-bit ALU and registers, memory of 4-bit nibbles, 12-bit
 * program counter, and instructions fetched as 16-bit big-endian words with the
 * top nibble clear.
 *
 * STATUS: machine layer only. Memory map, register file, port model and reset
 * are implemented and verified against BrickEmuPy's post-reset state. The
 * instruction table is NOT yet ported - see docs/E0C6200-SCOPE.md for the plan
 * and the remaining work.
 */

#ifndef E0C6200_H
#define E0C6200_H

#include <stdint.h>
#include <stddef.h>

#define E0C_RAM_SIZE        0x300   /* nibbles */
#define E0C_VRAM_SIZE       0x0A0   /* nibbles */
#define E0C_VRAM_PART1      0xE00
#define E0C_VRAM_PART2      0xE80
#define E0C_VRAM_PART_SIZE  0x050
#define E0C_IORAM_OFFSET    0xF00
#define E0C_IORAM_SIZE      0x07F

#define E0C_OSC1_CLOCK      32768u

/* Port indices, in the order the reference uses. */
enum {
    E0C_K0 = 0, E0C_K1,
    E0C_R0, E0C_R1, E0C_R2, E0C_R3, E0C_R4,
    E0C_P0, E0C_P1, E0C_P2, E0C_P3,
    E0C_PORT_COUNT
};

typedef struct e0c6200 e0c6200;

e0c6200 *e0c_new(const uint8_t *rom, size_t len, uint32_t clock_hz);
void     e0c_free(e0c6200 *c);
void     e0c_reset(e0c6200 *c);

/* Post-reset state, for differential comparison. */
typedef struct {
    uint16_t pc, npc;
    uint8_t  a, b, ix, iy, sp;
    uint8_t  cf, zf, df, if_;
    uint8_t  ram[E0C_RAM_SIZE];
    uint8_t  vram[E0C_VRAM_SIZE];
    uint8_t  pdir[E0C_PORT_COUNT];
    uint8_t  platch[E0C_PORT_COUNT];
    uint8_t  pullup[E0C_PORT_COUNT];
    uint8_t  pushpull[E0C_PORT_COUNT];
} e0c_state;

void e0c_snapshot(const e0c6200 *c, e0c_state *s);

/* Raw nibble access through the normal bus decode, for tests. */
uint8_t e0c_get_mem(e0c6200 *c, uint16_t addr);
void    e0c_set_mem(e0c6200 *c, uint16_t addr, uint8_t value);

/* Display RAM view for a front end. */
const uint8_t *e0c_vram(const e0c6200 *c);

#endif /* E0C6200_H */
