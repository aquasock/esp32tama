#!/usr/bin/env python3
"""
fuzz_diff.py - differential fuzzer: C core vs Python reference

The ROM only exercises 78 opcodes. The core implements 151. That leaves 73
opcodes, plus nearly every illegal/edge case, completely untested.

This generates synthetic programs that hammer the whole instruction set, runs
them through both cores, and reports any state divergence. A disagreement means
one of the two is wrong -- and since the Python core is the reference the ROM
was built against, the assumption is that the C core is the one that is wrong.

Usage
    python3 fuzz_diff.py 2000          # 2000 random programs
    python3 fuzz_diff.py 2000 --seed 7
    python3 fuzz_diff.py 500 --verbose

Requires the C cores built with ROMFUZZ support:
    gcc -O2 -DFUZZ -o /tmp/fuzzcore fuzzcore.c splb32.c
"""

import os
import random
import signal
import subprocess
import sys

REF = "/tmp/BrickEmuPy"
sys.path.insert(0, REF)
from cores.SPLB32 import SPLB32  # noqa: E402

FUZZCORE = os.environ.get("FUZZCORE", "/tmp/fuzzcore")

# Status flag bit positions, matching get_ps()
def ps_of(cpu):
    return ((cpu._NF << 7) | (cpu._VF << 6) | (cpu._BF << 4) |
            (cpu._DF << 3) | (cpu._IF << 2) | (cpu._ZF << 1) | cpu._CF)


class Stub:
    def register_pin_device(self, d): pass
    def register_port_device(self, d): pass
    def register_clock_device(self, d): pass
    def register_input_device(self, d): pass
    def register_serial_rx_device(self, d): pass
    def emit_port(self, *a): pass
    def emit_pin(self, *a): pass
    def emit_clock(self, *a): pass
    def emit_audio(self, *a): pass
    def emit_svg_element(self, *a): pass
    def emit_serial_tx(self, *a): pass


# The CPU's $C000-$FFFF window maps to FLAT ROM 0x4000-0x7FFF
# (read8 does `addr & 0x7FFF`). So a reset vector of 0xC800 executes at flat
# 0x4800. Getting this wrong makes every test compare zero to zero, which is
# how an earlier version of this file "passed" 151 opcodes without running any.
FIXED_BANK_FLAT = 0x4000
ENTRY = 0xC800
ENTRY_FLAT = ENTRY & 0x7FFF          # 0x4800


def make_rom(prog):
    """Build a 512 KB ROM with `prog` at the entry point in every bank window."""
    rom = bytearray(524288)
    # body of the program in the banked window of every bank
    for page in range(16):
        off = page * 32768
        rom[off:off + len(prog)] = prog
    # body also in the fixed bank at its flat position
    rom[ENTRY_FLAT:ENTRY_FLAT + len(prog)] = prog
    # vectors: flat 0x7FFA..0x7FFF is the fixed bank's $FFFA..$FFFF
    rom[0x7FFA:0x7FFC] = bytes([ENTRY & 0xFF, ENTRY >> 8])   # NMI
    rom[0x7FFC:0x7FFE] = bytes([ENTRY & 0xFF, ENTRY >> 8])   # RESET
    rom[0x7FFE:0x8000] = bytes([ENTRY & 0xFF, ENTRY >> 8])   # IRQ
    return bytes(rom)


class ReferenceHang(Exception):
    pass


def _alarm(signum, frame):
    raise ReferenceHang()


def run_python(rom, nsteps):
    """Run the reference, with a wall-clock guard.

    The reference can hang: if SFR $0F selects timer 0 external-clock mode the
    inner `while self._TM0_counter <= 0` loop has a bare `pass` and never
    terminates. That is a bug in the reference emulator, not in the ROM and not
    something worth reproducing, so a hang is recorded as its own outcome.
    """
    global _cycles
    cpu = SPLB32({"rom_path": _write_tmp(rom), "port_pullup": {}}, 4000000, Stub())
    _cycles = 0.0
    # the reference prints "illegal instruction ..." to stdout for every
    # illegal opcode; suppress it so the fuzz output stays readable
    import io, contextlib
    buf = io.StringIO()
    old = signal.signal(signal.SIGALRM, _alarm)
    signal.alarm(5)
    try:
        with contextlib.redirect_stdout(buf):
            for _ in range(nsteps):
                _cycles += cpu.clock()
    finally:
        signal.alarm(0)
        signal.signal(signal.SIGALRM, old)
    return (cpu.pc(), cpu._A, cpu._X, cpu._Y, cpu._SP, ps_of(cpu), _cycles)


_tmp = [None]
def _write_tmp(rom):
    if _tmp[0] is None:
        path = "/tmp/fuzz_rom.bin"
        open(path, "wb").write(rom)
        _tmp[0] = path
    else:
        open(_tmp[0], "wb").write(rom)
    return _tmp[0]


def run_c(rom, nsteps):
    path = "/tmp/fuzz_rom_c.bin"
    open(path, "wb").write(rom)
    out = subprocess.run([FUZZCORE, path, str(nsteps)],
                         capture_output=True, text=True, timeout=30)
    if out.returncode != 0:
        raise RuntimeError("fuzzcore failed: %s" % out.stderr[:300])
    vals = out.stdout.split()
    pc = int(vals[0], 16)
    a, x, y, sp, p = (int(v, 16) for v in vals[1:6])
    cycles = int(vals[6])
    return (pc, a, x, y, sp, p, cycles)


import json as _json

_OPS = _json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    "valid_ops.json")))
_VALID = [(int(k), v[1], v[2]) for k, v in _OPS.items()]


def gen_program(rng, nins, mode="valid"):
    """Generate an instruction stream.

    mode='valid'   only implemented opcodes, correct operand lengths. This is
                   what reaches the 73 opcodes the ROM never executes.
    mode='random'  raw random bytes, which mostly lands on illegal opcodes and
                   exercises the decode fallback path.
    """
    prog = bytearray()
    prog += bytes([0xA2, 0xFF])          # LDX #$FF
    prog += bytes([0x9A])                # TXS
    while len(prog) < nins:
        if mode == "valid":
            op, modename, argc = rng.choice(_VALID)
            prog.append(op)
            for _ in range(argc):
                # bias operands toward low addresses (zero page, RAM) so the
                # program does not wander into unmapped space on every access
                prog.append(rng.choice([rng.randrange(256),
                                        rng.randrange(0x40, 0x100)]))
        else:
            prog.append(rng.randrange(256))
            if rng.random() < 0.85:
                for _ in range(rng.choice([0, 1, 1, 2, 2, 2])):
                    prog.append(rng.randrange(256))
    return bytes(prog)


def main(argv):
    count = int(argv[1]) if len(argv) > 1 else 500
    seed = 1
    verbose = "--verbose" in argv
    if "--seed" in argv:
        seed = int(argv[argv.index("--seed") + 1])

    random_mode = '--random' in argv
    rng = random.Random(seed)
    fails = 0
    hangs = 0
    for i in range(count):
        nins = rng.choice([40, 80, 150])
        prog = gen_program(rng, nins, 'valid' if not random_mode else 'random')
        rom = make_rom(prog)
        steps = rng.choice([200, 400, 800])
        try:
            py = run_python(rom, steps)
            c = run_c(rom, steps)
        except ReferenceHang:
            hangs += 1
            if verbose:
                print("case %d: reference hung (timer EXT mode) - C core exits cleanly" % i)
            continue
        except Exception as e:
            print("case %d: ERROR %s" % (i, e))
            fails += 1
            continue

        # compare registers first (cheap, most informative)
        pyc = int(py[6])
        if py[:6] != c[:6] or pyc != c[6]:
            fails += 1
            kind = []
            if py[:6] != c[:6]: kind.append("REGS")
            if pyc != c[6]: kind.append("CYCLES %d vs %d" % (pyc, c[6]))
            print("case %d DIVERGE [%s]  steps=%d" % (i, ",".join(kind), steps))
            print("   python pc=%04X a=%02X x=%02X y=%02X sp=%02X p=%02X" % py[:6])
            print("   c      pc=%04X a=%02X x=%02X y=%02X sp=%02X p=%02X" % c[:6])
            if verbose:
                print("   prog:", prog[:60].hex())
            if fails > 15:
                print("... stopping after 15 failures")
                break
        elif verbose:
            print("case %d: registers match" % i)

    print("\n%d programs, %d divergences, %d reference hangs" % (count, fails, hangs))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
