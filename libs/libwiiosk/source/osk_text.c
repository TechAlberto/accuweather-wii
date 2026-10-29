/*-------------------------------------------------------------
osk_text.c -- proportional text metrics and text box layout
-------------------------------------------------------------*/
#include <stdlib.h>

#include "osk_internal.h"

/* system font cell the sizes refer to */
#define CELL_W		30.0f
#define CELL_H		36.0f
#define CELL_CAP	23.0f

const osk_font_size osk_text_font = { 32, 37, -1 };

/* Unpack the glyph atlas (osk_font_atlas_w * osk_font_atlas_h / 2 bytes,
   free() it when done). */
u8 *osk_font_unpack(void)
{
	return osk_unpack(osk_font_atlas_packed, osk_font_atlas_packed_size, NULL);
}

const osk_glyph *osk_find_glyph(uint32_t cp)
{
	unsigned lo = 0, hi = osk_font_nglyphs;

	if (cp == 0xA0)
		cp = ' ';
	while (lo < hi) {
		unsigned mid = (lo + hi) / 2;
		if (osk_font_glyphs[mid].cp == cp)
			return &osk_font_glyphs[mid];
		if (osk_font_glyphs[mid].cp < cp)
			lo = mid + 1;
		else
			hi = mid;
	}
	return NULL;
}

bool OSK_CanDisplay(uint32_t cp)
{
	return cp >= 0x20 && osk_find_glyph(cp) != NULL;
}

/* glyphs are scaled so their cap height matches the system font's */
float osk_glyph_scale_y(const osk_font_size *fs)
{
	return CELL_CAP * fs->sy / CELL_H / osk_font_cap;
}

static float scale_x(const osk_font_size *fs)
{
	return osk_glyph_scale_y(fs) * (fs->sx / CELL_W) / (fs->sy / CELL_H);
}

float osk_char_advance(uint32_t cp, const osk_font_size *fs)
{
	const osk_glyph *g = osk_find_glyph(cp);

	if (!g)
		g = osk_find_glyph('?');
	return g->adv16 / 16.0f * scale_x(fs) + fs->char_space;
}

float osk_text_width(const uint32_t *cps, unsigned n, const osk_font_size *fs)
{
	float w = 0;
	unsigned i;

	for (i = 0; i < n; i++)
		w += osk_char_advance(cps[i], fs);
	return n ? w - fs->char_space : 0;
}

float osk_utf8_width(const char *s, const osk_font_size *fs)
{
	uint32_t cp;
	size_t k;
	float w = 0;
	bool any = false;

	while ((k = OSK_UTF8Decode(s, &cp)) != 0) {
		w += osk_char_advance(cp, fs);
		s += k;
		any = true;
	}
	return any ? w - fs->char_space : 0;
}

uint32_t osk_display_char(const osk_keyboard *osk, unsigned index)
{
	uint32_t cp = osk->text[index];

	if ((osk->cfg.flags & OSK_FLAG_PASSWORD) && cp != '\n')
		return '*';
	return cp;
}

unsigned osk_wrap_text(const osk_keyboard *osk, unsigned start[OSK_MAX_CHARS + 2])
{
	unsigned i, n = 0;
	float x = 0;

	start[0] = 0;
	for (i = 0; i < osk->len; i++) {
		uint32_t cp = osk_display_char(osk, i);
		float adv;

		if (cp == '\n') {
			start[++n] = i + 1;
			x = 0;
			continue;
		}
		adv = osk_char_advance(cp, &osk_text_font);
		if (x > 0 && x + adv - osk_text_font.char_space > OSK_TEXT_W) {
			start[++n] = i;
			x = 0;
		}
		x += adv;
	}
	start[++n] = osk->len;
	return n;
}

unsigned osk_line_of(const unsigned *start, unsigned nlines, unsigned index)
{
	unsigned line = 0;

	while (line + 1 < nlines && start[line + 1] <= index)
		line++;
	return line;
}

float osk_caret_x(const osk_keyboard *osk, const unsigned *start, unsigned line, unsigned index)
{
	float x = 0;
	unsigned i;

	for (i = start[line]; i < index; i++) {
		uint32_t cp = osk_display_char(osk, i);
		if (cp != '\n')
			x += osk_char_advance(cp, &osk_text_font);
	}
	return x;
}
