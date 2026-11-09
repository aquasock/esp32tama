#!/usr/bin/env python3
"""
ref_trace.py - reference trace from the Python SPLB32 core

Emits the same format as `corebench --trace`, so the two can be diffed to prove
the C port executes identically to the reference implementation.

    python3 ref_trace.py ROM 200 5000 > py.trace   # lines, instrs-per-line
    python3 ref_trace.py ROM --final 4000000        # final state
"""
import sys
import os

BRICKEMUPY = os.environ.get("BRICKEMUPY", "/tmp/BrickEmuPy")
sys.path.insert(0, BRICKEMUPY)

from cores.SPLB32 import SPLB32  # noqa: E402


class Stub:
    """Minimal interconnect: no peripherals, no pin traffic."""
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


_EEPROM = [False]


def make_cpu(rom):
    # pull-ups from the real .brick config, so port reads match the harness
    mask = {"rom_path": rom, "port_pullup": {"PD": 2}}
    cpu = SPLB32(mask, 4000000, Stub())
    if _EEPROM[0]:
        # same wiring the .brick declares: HT24LC08 at 0xA0 on PD, SDA=2 SCL=1
        from peripherals.HT24LC08 import HT24LC08
        _eeprom[0] = HT24LC08({"path": None, "hw_address": 160, "port": "PD",
                               "SDA": 2, "SCL": 1}, cpu._interconnect)
    return cpu


_eeprom = [None]


def run_instrs(cpu, n, acc):
    """Run n opcodes; return accumulated cycles."""
    while cpu.istr_counter() < n:
        acc[0] += cpu.clock()
    return acc[0]


def ps(cpu):
    return ((cpu._NF << 7) | (cpu._VF << 6) | (cpu._BF << 4) |
            (cpu._DF << 3) | (cpu._IF << 2) | (cpu._ZF << 1) | cpu._CF)


def fnv(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def main(argv):
    rom = argv[1]
    if "--eeprom" in argv:
        _EEPROM[0] = True
    if "--final" in argv:
        n = int(argv[argv.index("--final") + 1])
        cpu = make_cpu(rom)
        acc = [0]
        run_instrs(cpu, n, acc)
        print("instrs", cpu.istr_counter())
        print("cycles", int(acc[0]))
        print("pc %04X" % cpu.pc())
        print("regs %02X %02X %02X %02X" % (cpu._A, cpu._X, cpu._Y, cpu._SP))
        print("ps %02X" % ps(cpu))
        print("ram_fnv %08X" % fnv(cpu._RAM))
        print("dpram_fnv %08X" % fnv(cpu._DPRAM))
        if _eeprom[0] is not None:
            print("eeprom_fnv %08X" % fnv(list(_eeprom[0]._mem)))
        return 0

    lines = int(argv[2])
    step = int(argv[3])
    cpu = make_cpu(rom)
    acc = [0]
    target = 0
    for _ in range(lines):
        target += step
        run_instrs(cpu, target, acc)
        print("%d %d %04X %02X %02X %02X %02X %02X" % (
            cpu.istr_counter(), int(acc[0]), cpu.pc(),
            cpu._A, cpu._X, cpu._Y, cpu._SP, ps(cpu)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
