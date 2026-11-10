/*
 * TamaLIB - A hardware agnostic first-gen Tamagotchi emulation library
 *
 * Copyright (C) 2021 Jean-Christophe Rona <jc@rona.fr>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "hw.h"
#include "cpu.h"
#include "hal.h"

/* SEG -> LCD mapping.
 *
 * These tables were MEASURED from the SVG face artwork that ships inside the
 * MAME ROM zips, not guessed: each segment's rectangle in the artwork gives its
 * panel coordinate. See assets/segmaps/ for the extraction and the data.
 *
 * Two results from that measurement, both worth knowing:
 *
 *   1. The whole Tamagotchi family shares one map. P1, P2, Angel and Mothra
 *      produce byte-identical tables, so the E0C6S46 and E0C6S48 split above is
 *      not a segment-mapping difference. Upstream's two arrays agree on
 *      everything the 32x16 panel uses.
 *
 *   2. Digimon is wired differently, and upstream's table gets it wrong -
 *      448 of 512 entries disagree. Rendering a Digimon ROM with the
 *      Tamagotchi table gives a plausible but incorrect creature (47 lit
 *      pixels, ragged outline); the measured map gives 65 and a coherent
 *      shape. The measured map is corroborated 512/512 by an independent
 *      derivation from the ROM's own disassembly
 *      (willmcginnis/digimon-vpet-1997-disassembly, data/lcd_layout.json).
 *
 * Values are segment -> x. Any value >= LCD_WIDTH means "icon segment": the
 * icon index is chosen by the seg == 8 / seg == 28 test below, so collapsing
 * icon slots to 32 is safe.
 */
#if defined(E0C6S48_SUPPORT)
/* 51 segments; Tamagotchi family map, measured */
static u8_t seg_pos[MEM_DISPLAY1_SIZE/2] = {
	0, 1, 2, 3, 4, 5, 6, 7, 32, 8, 9, 10, 11, 12, 13, 14, 15, 32, 32, 32,
	31, 30, 29, 28, 27, 26, 25, 24, 32, 23, 22, 21, 20, 19, 18, 17, 16
};
#elif defined(E0C6S46_SUPPORT)
/* 40 segments; same table, which is the point */
static u8_t seg_pos[MEM_DISPLAY1_SIZE/2] = {
	0, 1, 2, 3, 4, 5, 6, 7, 32, 8, 9, 10, 11, 12, 13, 14, 15, 32, 32, 32,
	31, 30, 29, 28, 27, 26, 25, 24, 32, 23, 22, 21, 20, 19, 18, 17, 16
};
#endif

/* Digimon uses a different segment order. Selected at runtime by
 * hw_set_segmap() so one build can run either family. */
/* Sized to MEM_DISPLAY1_SIZE/2 like seg_pos itself, and padded with 32 (icon)
 * rather than left short: hw_set_lcd_pin() indexes this by `seg`, which runs to
 * 39 or 50, so a 37-entry table would be read out of bounds. That showed up as
 * devices halting at random addresses. */
static const u8_t seg_pos_digimon[MEM_DISPLAY1_SIZE/2] = {
	32, 31, 30, 29, 28, 27, 26, 25, 24, 32, 32, 32, 23, 22, 21, 20, 19, 18,
	17, 16, 0, 1, 2, 3, 4, 5, 6, 7, 32, 8, 9, 10, 11, 12, 13, 14, 15,
	32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32
};

static const u8_t *seg_pos_active = seg_pos;

/* Some devices number their commons the other way up. Digimon does: measured
 * over all 512 segment bits, its panel coordinate is exactly 15 - com, while
 * the Tamagotchi family's is exactly com. So this is a per-device axis
 * inversion, not a global one - flipping everywhere would turn the Tamagotchi
 * egg upside down, which is exactly the mistake not to make here. */
static u8_t seg_com_flip = 0;

/* Active panel width. 32 for the Tamagotchi family, 39 for Alien Fever. Kept
 * separate from LCD_WIDTH, which is now the widest supported panel, so the
 * Tamagotchi maps keep treating seg_pos values >= 32 as icons. */
static u8_t seg_pixel_width = 32;

u8_t hw_get_pixel_width(void) { return seg_pixel_width; }

/* Identity map: x = segment index, no permutation.
 *
 * MAME's e0c6s46_device::screen_update() lays the panel out as
 *   render_buf[com * width + seg]
 * where width is the segment count. So on hardware x is simply the segment
 * index and there is no lookup table at all. The Tamagotchi devices have a
 * physical quirk that makes their x order non-identity - measured from their
 * artwork - but devices that follow the plain layout need this instead.
 *
 * Alien Fever is the case in point: MAME describes it as a 39x16 panel, and
 * drawing it through the 32-wide Tamagotchi permutation scrambles it. */
static u8_t seg_pos_identity[MEM_DISPLAY1_SIZE/2];

void hw_set_segmap(hw_segmap_t which)
{
	if (which == HW_SEGMAP_IDENTITY) {
		for (u8_t i = 0; i < sizeof(seg_pos_identity); i++)
			seg_pos_identity[i] = i;
		seg_pos_active = seg_pos_identity;
		seg_com_flip = 0;
		seg_pixel_width = 39;   /* Alien Fever: 39x16, MAME hh_e0c6200.cpp */
		return;
	}
	if (which == HW_SEGMAP_DIGIMON) {
		seg_pos_active = seg_pos_digimon;
		seg_com_flip = 1;
		seg_pixel_width = 32;
	} else {
		seg_pos_active = seg_pos;
		seg_com_flip = 0;
		seg_pixel_width = 32;
	}
}

bool_t hw_init(void)
{
	/* Buttons/Tap sensor are active LOW */
	cpu_set_input_pin(PIN_K00, PIN_STATE_HIGH);
	cpu_set_input_pin(PIN_K01, PIN_STATE_HIGH);
	cpu_set_input_pin(PIN_K02, PIN_STATE_HIGH);
	cpu_set_input_pin(PIN_K03, PIN_STATE_HIGH);

	return 0;
}

void hw_release(void)
{
}

void hw_set_lcd_pin(u8_t seg, u8_t com, u8_t val)
{
	//printf("   hw_set_lcd_pin: seg = %u, com = %u, val = %u\n", seg, com, val);
	if (seg_pos_active[seg] < seg_pixel_width) {
		g_hal->set_lcd_matrix(seg_pos_active[seg],
		                        seg_com_flip ? (LCD_HEIGHT - 1 - com) : com, val);
	} else {
		/*
		 * IC n -> seg-com|...
		 * IC 0 ->  8-0 |18-3 |19-2
		 * IC 1 ->  8-1 |17-0 |19-3
		 * IC 2 ->  8-2 |17-1 |37-12|38-13|39-14
		 * IC 3 ->  8-3 |17-2 |18-1 |19-0
		 * IC 4 -> 28-12|37-13|38-14|39-15
		 * IC 5 -> 28-13|37-14|38-15
		 * IC 6 -> 28-14|37-15|39-12
		 * IC 7 -> 28-15|38-12|39-13
		 */
		if (seg == 8 && com < 4) {
			g_hal->set_lcd_icon(com, val);
		} else if (seg == 28 && com >= 12) {
			g_hal->set_lcd_icon(com - 8, val);
		}
	}
}

void hw_set_button(button_t btn, btn_state_t state)
{
	pin_state_t pin_state = (state == BTN_STATE_PRESSED) ? PIN_STATE_LOW : PIN_STATE_HIGH;

	switch (btn) {
		case BTN_TAP:
			cpu_set_input_pin(PIN_K03, pin_state);
			break;

		case BTN_LEFT:
			cpu_set_input_pin(PIN_K02, pin_state);
			break;

		case BTN_MIDDLE:
			cpu_set_input_pin(PIN_K01, pin_state);
			break;

		case BTN_RIGHT:
			cpu_set_input_pin(PIN_K00, pin_state);
			break;
	}
}

void hw_set_buzzer_freq(u4_t freq)
{
	u32_t snd_freq = 0;

	switch (freq) {
		case 0:
			/* 4096.0 Hz */
			snd_freq = 40960;
			break;

		case 1:
			/* 3276.8 Hz */
			snd_freq = 32768;
			break;

		case 2:
			/* 2730.7 Hz */
			snd_freq = 27307;
			break;

		case 3:
			/* 2340.6 Hz */
			snd_freq = 23406;
			break;

		case 4:
			/* 2048.0 Hz */
			snd_freq = 20480;
			break;

		case 5:
			/* 1638.4 Hz */
			snd_freq = 16384;
			break;

		case 6:
			/* 1365.3 Hz */
			snd_freq = 13653;
			break;

		case 7:
			/* 1170.3 Hz */
			snd_freq = 11703;
			break;
	}

	if (snd_freq != 0) {
		g_hal->set_frequency(snd_freq);
	}
}

void hw_enable_buzzer(bool_t en)
{
	g_hal->play_frequency(en);
}
