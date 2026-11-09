#!/usr/bin/env python3
"""
disasm.py - GPLB (Tama-Go) ROM disassembler

The GPLB52320A/52640A has a 16 KB fixed block at $C000-$FFFF and a 32 KB bank
window at $4000-$BFFF selected by the register at $3000. A dumped image is laid
out as:

    [0x00000:0x04000]  fixed block, $C000-$FFFF
    [0x04000:0xA4000]  20 banks of 32 KB, bank N at $4000-$BFFF

Usage:
    ./disasm.py rom.bin 0xCC00 40              # fixed block, from $CC00
    ./disasm.py rom.bin --bank 1 0x5900 20     # bank 1 (window) from $5900
    ./disasm.py rom.bin --vectors              # dump the vector area

Addresses may be given as $CC00, 0xCC00 or CC00.

The opcode table is tools/gplb_ops.json, parsed once from the bytecoden.txt
shipped with natashenka/Tamagotchi-Hack. Standard 6502 opcode numbers; this
chip uses them unchanged (verified by running Klaus Dormann's functional test
against core/gplb.c).
"""
import json
import os
import sys

FIXED_SIZE = 0x4000
BANK_SIZE = 0x8000
FIXED_BASE = 0xC000
WIN_BASE = 0x4000

TBL = {}
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "gplb_ops.json")) as fh:
    for row in json.load(fh):
        # row = [?, mnemonic, opcode, length, cycles]
        TBL[row[2]] = (row[1], row[3])


def parse_addr(text):
    t = text.strip().replace("$", "")
    if t.lower().startswith("0x"):
        t = t[2:]
    return int(t, 16)


def file_offset(rom, addr, bank):
    """Map a CPU address to an offset in the dumped image."""
    if addr >= FIXED_BASE:
        return addr - FIXED_BASE
    bank_off = FIXED_SIZE + bank * BANK_SIZE + (addr - WIN_BASE)
    return bank_off


def fmt_operand(mnem, pc, b1, b2):
    if "#dd" in mnem:
        return mnem.replace("#dd", "#$%02X" % b1)
    if "aaaa" in mnem:
        return mnem.replace("aaaa", "$%04X" % (b1 | (b2 << 8)))
    if "aa" in mnem:
        return mnem.replace("aa", "$%02X" % b1)
    if "rr" in mnem:
        d = b1 - 256 if b1 > 127 else b1
        return mnem.replace("rr", "$%04X" % ((pc + 2 + d) & 0xFFFF))
    return mnem


def disassemble(rom, start, count, bank=0):
    out = []
    addr = start
    for _ in range(count):
        off = file_offset(rom, addr, bank)
        if off < 0 or off >= len(rom):
            break
        op = rom[off]
        mnem, length = TBL.get(op, ("??? .byte $%02X" % op, 1))
        b1 = rom[off + 1] if off + 1 < len(rom) else 0
        b2 = rom[off + 2] if off + 2 < len(rom) else 0
        raw = " ".join("%02X" % rom[off + i] for i in range(length)
                       if off + i < len(rom))
        out.append("%04X: %-10s %s" % (addr, raw, fmt_operand(mnem, addr, b1, b2)))
        addr = (addr + length) & 0xFFFF
    return "\n".join(out)


def dump_vectors(rom):
    """The interesting tables: peripheral vectors and the hardware vectors."""
    lines = ["peripheral interrupt vectors $FFC0-$FFDF:"]
    for i in range(16):
        a = 0xFFC0 + i * 2
        o = file_offset(rom, a, 0)
        v = rom[o] | (rom[o + 1] << 8)
        lines.append("  vector[%2d] $%04X = $%04X" % (i, a, v))
    lines.append("hardware vectors:")
    for name, a in (("NMI", 0xFFFA), ("RESET", 0xFFFC), ("IRQ", 0xFFFE)):
        o = file_offset(rom, a, 0)
        v = rom[o] | (rom[o + 1] << 8)
        lines.append("  %-5s $%04X = $%04X" % (name, a, v))
    return "\n".join(lines)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    rom = open(argv[1], "rb").read()

    if "--vectors" in argv:
        print(dump_vectors(rom))
        return 0

    bank = 0
    rest = argv[2:]
    if "--bank" in rest:
        i = rest.index("--bank")
        bank = int(rest[i + 1])
        rest = rest[:i] + rest[i + 2:]

    if len(rest) < 2:
        print(__doc__)
        return 2
    start = parse_addr(rest[0])
    count = int(rest[1], 0)
    print(disassemble(rom, start, count, bank))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
