import sys, json, subprocess, os
sys.path.insert(0,'/tmp/BrickEmuPy')
from cores.SPLB32 import SPLB32

class Stub:
    def register_pin_device(self,d): pass
    def register_port_device(self,d): pass
    def register_clock_device(self,d): pass
    def register_input_device(self,d): pass
    def register_serial_rx_device(self,d): pass
    def emit_port(self,*a): pass
    def emit_pin(self,*a): pass
    def emit_clock(self,*a): pass
    def emit_audio(self,*a): pass
    def emit_svg_element(self,*a): pass
    def emit_serial_tx(self,*a): pass

ops = json.load(open('valid_ops.json'))

ENTRY = 0xC800
ENTRY_FLAT = ENTRY & 0x7FFF   # 0x4800 -- $C800 maps to flat 0x4800

def make_rom(prog):
    rom = bytearray(524288)
    for page in range(16):
        rom[page*32768: page*32768+len(prog)] = prog
    rom[ENTRY_FLAT:ENTRY_FLAT+len(prog)] = prog
    v = bytes([ENTRY & 0xFF, ENTRY >> 8])
    rom[0x7FFA:0x7FFC] = v
    rom[0x7FFC:0x7FFE] = v
    rom[0x7FFE:0x8000] = v
    return bytes(rom)

def ps_of(c):
    return ((c._NF<<7)|(c._VF<<6)|(c._BF<<4)|(c._DF<<3)|(c._IF<<2)|(c._ZF<<1)|c._CF)

# prologue: SP=FF, fill zero page with varied values so ops have something to eat
PRE = bytes([0xA2,0xFF, 0x9A, 0xA9,0x55, 0xA0,0x10, 0xA2,0x08])

bad=[]
for opstr,(name,mode,argc) in sorted(ops.items(), key=lambda kv:int(kv[0])):
    op=int(opstr)
    body=bytes([op])
    for i in range(argc):
        body += bytes([0x42 if i==0 else 0x00])
    prog = PRE + body + bytes([0xEA,0xEA,0xEA])
    rom = make_rom(prog)
    open('/tmp/ot.bin','wb').write(rom)

    # python: step through prologue+op, capture state right after the op
    cpu = SPLB32({"rom_path":"/tmp/ot.bin","port_pullup":{}},4000000,Stub())
    acc=0.0
    npre = 5   # LDX, TXS, LDA, LDY, LDX
    for _ in range(npre+1):
        acc += cpu.clock()
    py=(cpu.pc(), cpu._A, cpu._X, cpu._Y, cpu._SP, ps_of(cpu), int(acc))

    r=subprocess.run(['/tmp/fuzzcore','/tmp/ot.bin',str(npre+1)],capture_output=True,text=True)
    v=r.stdout.split()
    c=(int(v[0],16),int(v[1],16),int(v[2],16),int(v[3],16),int(v[4],16),int(v[5],16),int(v[6]))
    if py!=c:
        bad.append((opstr,name,py,c))

print(f"tested {len(ops)} opcodes, {len(bad)} mismatches")
for op,name,py,c in bad[:20]:
    print(f"  {op} {name:16s}")
    print(f"     py pc={py[0]:04X} a={py[1]:02X} x={py[2]:02X} y={py[3]:02X} sp={py[4]:02X} p={py[5]:02X} cyc={py[6]}")
    print(f"     c  pc={c[0]:04X} a={c[1]:02X} x={c[2]:02X} y={c[3]:02X} sp={c[4]:02X} p={c[5]:02X} cyc={c[6]}")
