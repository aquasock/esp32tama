# Pico 2 pin assignment

Pin numbers verified against the **Raspberry Pi Pico 2 Datasheet**, Figure 2
("The pinout of the Raspberry Pi Pico 2 board"). Not from memory.

Physical layout: pins 1–20 run down the left edge, 21–40 up the right edge —
so **pin 40 is top-right and pin 21 is bottom-right**. USB at the top.

Read-only harness. The Tamagotchi keeps its own CR2032; the Pico contributes
GND and listening pins only.

## The six connections

| Tama pad | Pico 2 pin | GPIO | Role |
|---|---|---|---|
| `GND` | **3** | GND | ground reference |
| `CLK` | **4** | GP2 | clock capture |
| `DATA` | **5** | GP3 | data capture |
| `P0.12` | **7** | GP5 | unknown, input only |
| `VDD` | **31** | GP26 / ADC0 | rail sense only |
| `RESET2` | **6** | GP4 | reset — connect last, see warning |

**No resistors. No capacitor. No divider.** An earlier draft of this document
called for a 330 ohm series resistor on every line and a 1M/100k divider on
`VDD`. Both were over-engineering and have been removed:

- Series resistors only matter if a pin might become an output. Every pin here
  is an input, and the firmware never drives them.
- `VDD` is ~3.0 V and the ADC reference is 3.3 V, so it reads directly.

## Why pin 3 for GND

Pin 3 sits immediately above pin 4 (`CLK`), which puts GND at the head of one
contiguous run: **3, 4, 5, 6, 7**. Four signals and their ground on five
adjacent pins — short wires, tight ground return, no crossing.

Any of pins 8, 12, 18, 23, 28, 33, 38 work identically if pin 3 is awkward.

`VDD` goes to the right-hand side at pin 31 to keep the analog line away from
the clocked signals.

## WARNING — RP2350 GPIOs pull DOWN at power-up

Raspberry Pi's *Hardware design with RP2350* states: "GPIOs default to
pull-downs at power-up."

`RESET2` is active low. Wired straight to a GPIO, it holds the Tamagotchi in
reset from the moment the Pico powers on until firmware reconfigures the pad.
The device simply never boots, and the symptom looks like a dead board.

A series resistor does **not** fix this — a resistor into a pull-down is still
a pull-down.

Therefore:

1. **First test: leave the `RESET2` lead unplugged.** Sniffing needs no reset.
2. When reset control is needed, set `Pin(4, Pin.IN, Pin.PULL_UP)` as the very
   first statement in `main.py`.
3. Or connect through a jumper and close it only after the Pico has booted.

The same pull-down applies to `P0.12` on pin 7. Harmless until we know what
that pin does, but note that its state at *Tamagotchi* power-up is what would
matter if it is a boot-mode pin — not its state at Pico power-up.

## Power order

Pico USB first, **then** insert the battery. Reverse when packing up.

A live signal into an unpowered RP2350 back-feeds it through the ESD
protection diodes. On a CR2032 that means slow drain and erratic behaviour.

## GPIO choice rationale

`CLK` on GP2 and `DATA` on GP3 are adjacent, which lets PIO use a single
`in pins, 1` with `in_base` set to GP3.

## Do not

- Do not connect Pico `3V3` (pin 36) to the board's `VDD` while a cell is fitted.
- Do not drive `VPP` — it is unbonded anyway.
- Do not configure GP26 as a digital output; it is the `VDD` sense input.
- Do not hard-wire `RESET2` without handling the power-up pull-down.

## Reference

`wiring/pico2-header-map.png` — the physical header with all six marked.
