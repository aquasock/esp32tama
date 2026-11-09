# Emulation completeness audit

Answer to "is the emulator emulating everything properly?" — **no, not
everything.** The CPU and display are complete and verified bit-exact. Two
peripherals are missing, and one of them is observable in machine state.

## What is implemented and verified

| Area | Status | Evidence |
|---|---|---|
| CPU, 151 opcodes | **bit-exact** | 10,000,000 instructions, identical PC/regs/flags/cycles vs the Python reference |
| ROM banking, 16 banks | bit-exact | same run |
| SFR bank, all 64 addresses | bit-exact | all 64 readbacks identical at matched emulated time |
| Timers TM0, TM1, 1 kHz timebase | works | TM1 matches exactly; TM0 drifts — see below |
| IRQ, NMI | bit-exact | same run |
| Sleep/wake, cycle batching | bit-exact | fractional tick accumulation matches |
| RAM 1216 B, DPRAM 512 B | bit-exact | FNV hash identical |
| Ports A–D, direction/attr/pull-ups | works | PA buttons confirmed in play |
| Buttons (PA 0/1/2) | works | confirmed by the operator: clock set, medicine, cleaning, praise |
| EEPROM HT24LC08 | **bit-exact** | identical eeprom hash; verified against the reference |
| LCD, 970 segments | works | IoU 0.93 vs an independent SVG render |
| Save state | works | fork-and-compare identical at 3,000,000 instructions |

## Gap 1 — Infrared. **CLOSED** (was: missing entirely, and observable.)

The V3's only data interface is a 38 kHz IR link used for connecting two
devices. The reference implements it in `peripherals/CON_V3_IR.py`: a port
device registered on **PB** that drives the receiver pin and decodes burst/space
timing.

*(Original finding, kept for the record: my core had no IR, which showed up in
state as below. Ported and verified — see "Infrared, ported" at the end.)*

```
after 40,000,009 cycles (10 emulated seconds), matched instruction count:

                    mine      reference
PB (0x0A)           0x01      0x33      <- receiver pin state
NMI_CTRL (0x38)     0x81      0x82      <- ROM configured it differently
TM0 low/high (0x10) 0xFD86    0xFBB9    <- timer 0 drifts
```

Reading PB differently makes the ROM take a different configuration decision,
which changes NMI_CTRL, which changes timer behaviour. The ROM's *control flow*
still matched over the same period — PC and instruction count were identical —
so this is a subtle difference in machine state rather than a visible behaviour
change in single-player.

### What it costs

- **No connection play.** Connecting two Tamas is the entire point of the
  "Connection" product line. Two emulated devices cannot talk.
- Any ROM path that inspects the IR hardware sees nothing attached.
- Small state divergence, as above.

### What it would take

Port `CON_V3_IR` as a port device on PB: burst/space cycle counters, a receive
state machine driven off the interconnect clock, and byte-level transmit. About
the same size as the EEPROM port (150 lines). The timing constants are already
in the reference:

```
BURST_AGC_CYCLES    38232      BURST_BIT_CYCLES      1768
SPACE_AGC_CYCLES     9908      SPACE_BIT0_CYCLES     3088
BURST_END_CYCLES     4680      SPACE_BIT1_CYCLES     5508
BURST_PULSE_CYCLES    104
```

Verification would be the same procedure as everything else: differential
against the reference, then confirm PB and NMI_CTRL converge.

## Gap 2 — Audio. **CLOSED** (was: missing, and the ROM actively uses it.)

The reference generates audio events through `SPLB32sound`: PWM amplitude or
tone envelope on two channels, toggled from the timer overflow path.

*(Original finding: the reference produced 1751 audio events in 3 emulated
seconds while my core produced none. Ported and verified — see "Audio, ported"
at the end.)*

```
                    mine      reference
audio events        0         1751 per 3 s
AUD0_CTRL           0xC8      0xC8      <- registers match
```

### What it costs

- Silence. Every beep, chirp and the death jingle are missing.
- Nothing in the game logic depends on audio being audible, so behaviour is
  otherwise unaffected.

### What it would take

`SPLB32sound` is 25 lines: an envelope per channel, a toggle state, and an emit
call from the TM0/TM1 overflow path. The formant is `(pitch, is_noise,
amplitude, sweep)` — the reference does not model the actual waveform, so
matching it is straightforward. A front end then needs an audio backend.

## Gap 3 — The EEPROM is never written (ROM behaviour, not a gap in my core)

Verified in both implementations: with the EEPROM attached, driving buttons and
running five emulated minutes, there are 1497 bits of I²C traffic and **zero
writes**. The ROM reads the EEPROM but does not write it during normal play.

So persistence comes from the emulator's machine save state, not from the
EEPROM path. Whether real hardware writes under conditions we cannot drive — a
long age-out, a shutdown hook — is unresolved.

## Summary

**Complete and trustworthy:** CPU, memory, display, timers, interrupts, ports,
buttons, EEPROM reads, save state. Bit-exact where measurable.

**Missing:** IR (observable, and blocks the product's headline feature) and
audio (silence only).

Neither is a correctness bug in what is implemented — the divergence is entirely
explained by absence. But "emulating everything" would need both.


---

# Infrared, ported (2026-11-09)

`con_v3_ir.c` implements the link from the reference's `CON_V3_IR.py`: bearer
timing, receive state machine, and byte reconstruction on transmit.

## What it does

- **Receive**: bytes queued with `splb32_ir_push` are replayed to the firmware as
  timed bursts on PB bit 2, so the ROM's own decoder does the work.
- **Transmit**: the firmware toggling PC bit 2 is measured and reconstructed into
  bytes, retrievable with `splb32_ir_pop_tx`.

Timing constants come from the reference so the ROM decodes what it expects.

## Verification

| Test | Result |
|---|---|
| RX pin sequence, 300,000 cycles, 2 bytes replayed | **78,677 transitions, identical to the reference** |
| TX byte reconstruction, same drive sequence | `0x65` from both |
| All 64 SFRs + PC after 40,000,000 cycles with IR attached | **identical to the reference** |
| ROM at 10,000,000 instructions | still bit-exact |
| 151 opcodes, differential | 0 mismatches |
| 200 fuzzed programs | 0 divergences |

A note on method: my first TX unit test "failed" (decoded `0x65`, expected `0xB2`)
and the impulse was to fix the model. Running the identical sequence against the
reference produced `0x65` as well — the model was right and the test's framing
assumption was wrong. Differential tests earn their keep.

## Two more reference bugs found while wiring it up

Both were discovered because attaching IR made PB reads diverge, and chasing that
uncovered two unrelated getter/setter discrepancies:

- **PB direction is expanded.** `_set_sfr_io_portB_dir` spreads four written
  bits into eight enable bits: `5` becomes `0x33`. Storing the raw value made PB
  read `0x01` instead of `0x33`, which changed the ROM's configuration decisions
  and shifted `NMI_CTRL` and the whole timer-0 state.
- **NMI control returns a composite.** `_get_sfr_nmi_ctrl` returns
  `NMIREQ | (NMI_CTRL & 0x80)`, not the raw register. Returning the raw value
  made the ROM read the wrong thing and branch differently.

## One more of mine

Timer 0 drifted by ~430 counts over 10 emulated seconds. Cause: I truncated the
sleep tick to an integer before adding it to TM0, while the reference adds the
fractional `122.0703125`. Over thousands of idle ticks that accumulates. TM0 and
TM1 are now `double`, matching the reference, and truncated only when read.

## Still open: no automated end-to-end link test

Two emulated instances relay bytes to each other under `splb32_ir_push` /
`splb32_ir_pop_tx`, but I could not drive either ROM far enough to actually
initiate a connection — that needs menu navigation I cannot automate reliably.
The bearer is verified; the *protocol* between two instances is not.


---

# Audio, ported (2026-11-09)

`splb32_sound.c` implements both channels from the reference's `SPLB32sound.py`:
PWM amplitude on a data write, tone envelope, and a toggle on timer overflow.

## The detail that matters

In PWM mode the reference maps the register `0..255` onto amplitude `-1..+1` as
`(data / 128) - 1`. That is asymmetric — **128 is silence**, not 127.5, and the
range runs -1.0 to +0.992. Getting this wrong shifts the DC offset of everything
the device says.

## Verification: event counts, second by second

| emulated s | mine | reference |
|---|---|---|
| 1 | 1454 | 1454 |
| 2 | 1705 | 1705 |
| 3 | 1751 | 1751 |
| 4 | 1797 | 1797 |
| 5 | 1843 | 1843 |
| 6 | 1889 | 1889 |

Exact at every sample, and the channel output level matches too (`+1.000` at
t=1 s, silence after — the ROM beeps once at boot and then waits).

## One bug in my first attempt

My emit counter read +2 higher at every sample while the *rate* matched exactly
(251 events per second in both). Cause: I incremented on every audio-data write,
but the reference only emits in tone mode when the envelope is being cleared.
Fixed by mirroring the reference's emit points. The rate being right while the
offset was wrong is what pointed at counting rather than timing.

## Front end

SDL2 audio at 22050 Hz stereo. The core supplies a level per channel; the front
end squares it into a tone. The reference does not model a waveform either, so
this is faithful to what the emulator knows, not to the silicon.

## Regression check

Core still bit-exact at 10,000,000 instructions, 151 opcodes with 0 mismatches.
