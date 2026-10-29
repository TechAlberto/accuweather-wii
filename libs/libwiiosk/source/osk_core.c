/*-------------------------------------------------------------
osk_core.c -- keyboard state, text editing and input handling
-------------------------------------------------------------*/
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "osk_internal.h"

static const osk_theme default_theme = {
	.overlay		= 0x00000000,
	.panel			= 0xFFFFFFFF,
	.key_top		= 0xFFFFFFFF,
	.key_bottom		= 0xDCE3EAFF,
	.special_top		= 0xD2DAE2FF,
	.special_bottom		= 0xB0BCC8FF,
	.focus_top		= 0xE0E0FFFF,
	.focus_bottom		= 0xE0E0FFFF,
	.active			= 0xFFFF20FF,
	.key_text		= 0x000000FF,
	.key_text_disabled	= 0xA8A8A8FF,
	.textbox		= 0xFFFFFFFF,
	.textbox_border		= 0x34BEEDFF,
	.text			= 0x404040FF,
	.caret			= 0xFF3232FF,
	.title			= 0xBEBEBEFF,
	.hint			= 0x707A86FF,
};

const osk_theme *OSK_DefaultTheme(void)
{
	return &default_theme;
}

void OSK_DefaultConfig(osk_config *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->max_chars = 64;
	cfg->layout = OSK_LAYOUT_QWERTY;
	cfg->screen_width = 640;
	cfg->screen_height = 480;
}

/*-------------------------------------------------------------
  UTF-8
-------------------------------------------------------------*/
size_t OSK_UTF8Encode(uint32_t cp, char out[4])
{
	if (cp < 0x80) {
		out[0] = cp;
		return 1;
	}
	if (cp < 0x800) {
		out[0] = 0xC0 | (cp >> 6);
		out[1] = 0x80 | (cp & 0x3F);
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = 0xE0 | (cp >> 12);
		out[1] = 0x80 | ((cp >> 6) & 0x3F);
		out[2] = 0x80 | (cp & 0x3F);
		return 3;
	}
	out[0] = 0xF0 | ((cp >> 18) & 0x07);
	out[1] = 0x80 | ((cp >> 12) & 0x3F);
	out[2] = 0x80 | ((cp >> 6) & 0x3F);
	out[3] = 0x80 | (cp & 0x3F);
	return 4;
}

size_t OSK_UTF8Decode(const char *str, uint32_t *cp)
{
	const u8 *s = (const u8 *)str;
	size_t n, i;
	uint32_t c;

	if (!s || !s[0])
		return 0;
	if (s[0] < 0x80) {
		*cp = s[0];
		return 1;
	}
	if ((s[0] & 0xE0) == 0xC0) {
		n = 2; c = s[0] & 0x1F;
	} else if ((s[0] & 0xF0) == 0xE0) {
		n = 3; c = s[0] & 0x0F;
	} else if ((s[0] & 0xF8) == 0xF0) {
		n = 4; c = s[0] & 0x07;
	} else {
		/* stray byte: treat as Latin-1 */
		*cp = s[0];
		return 1;
	}
	for (i = 1; i < n; i++) {
		if ((s[i] & 0xC0) != 0x80) {
			*cp = s[0];
			return 1;
		}
		c = (c << 6) | (s[i] & 0x3F);
	}
	*cp = c;
	return n;
}

/*-------------------------------------------------------------
  helpers
-------------------------------------------------------------*/
static bool multiline(const osk_keyboard *osk)
{
	return (osk->cfg.flags & OSK_FLAG_MULTILINE) != 0;
}

static void emit(osk_keyboard *osk, osk_event ev)
{
	if (osk->cfg.on_event)
		osk->cfg.on_event(osk, ev, osk->cfg.user);
}

void (*osk_sound_hook)(osk_sound sound) = NULL;

static void sound(osk_keyboard *osk, osk_sound s)
{
	if (osk->cfg.on_sound)
		osk->cfg.on_sound(osk, s, osk->cfg.user);
	else if (osk_sound_hook)
		osk_sound_hook(s);
}

static void reject(osk_keyboard *osk)
{
	emit(osk, OSK_EVENT_REJECT);
	sound(osk, OSK_SOUND_ERROR);
}

bool osk_accepts(const osk_keyboard *osk, uint32_t cp)
{
	if (!cp)
		return false;
	if (cp == '\n') {
		if (!multiline(osk))
			return false;
	} else if (!OSK_CanDisplay(cp)) {
		return false;
	}
	if (osk->cfg.filter && !osk->cfg.filter(cp, osk->cfg.user))
		return false;
	return true;
}

/* keep the caret's line inside the two visible lines */
static void ensure_caret_visible(osk_keyboard *osk)
{
	unsigned start[OSK_MAX_CHARS + 2];
	unsigned n = osk_wrap_text(osk, start);
	unsigned line = osk_line_of(start, n, osk->caret);

	if (line < osk->scroll)
		osk->scroll = line;
	if (line >= osk->scroll + OSK_TEXT_LINES)
		osk->scroll = line - OSK_TEXT_LINES + 1;
	if (osk->scroll + OSK_TEXT_LINES > n)
		osk->scroll = n > OSK_TEXT_LINES ? n - OSK_TEXT_LINES : 0;
}

/* insert at the caret without any checks */
static void put_char(osk_keyboard *osk, uint32_t cp)
{
	memmove(&osk->text[osk->caret + 1], &osk->text[osk->caret],
		(osk->len - osk->caret) * sizeof(uint32_t));
	osk->text[osk->caret++] = cp;
	osk->len++;
	osk->utf8_dirty = true;
}

static bool insert_char(osk_keyboard *osk, uint32_t cp)
{
	if (cp == 0xA0)
		cp = ' ';
	if (osk->len >= osk->max_chars || !osk_accepts(osk, cp)) {
		reject(osk);
		return false;
	}
	put_char(osk, cp);
	sound(osk, OSK_SOUND_INPUT);
	return true;
}

static void backspace(osk_keyboard *osk)
{
	if (!osk->caret) {
		reject(osk);
		return;
	}
	memmove(&osk->text[osk->caret - 1], &osk->text[osk->caret],
		(osk->len - osk->caret) * sizeof(uint32_t));
	osk->caret--;
	osk->len--;
	osk->utf8_dirty = true;
	sound(osk, OSK_SOUND_DELETE);
}

static void delete_char(osk_keyboard *osk)
{
	if (osk->caret >= osk->len) {
		reject(osk);
		return;
	}
	memmove(&osk->text[osk->caret], &osk->text[osk->caret + 1],
		(osk->len - osk->caret - 1) * sizeof(uint32_t));
	osk->len--;
	osk->utf8_dirty = true;
	sound(osk, OSK_SOUND_DELETE);
}

static void confirm(osk_keyboard *osk)
{
	if ((osk->cfg.flags & OSK_FLAG_REQUIRE_TEXT) && !osk->len) {
		reject(osk);
		return;
	}
	osk->status = OSK_STATUS_DONE;
	sound(osk, OSK_SOUND_OK);
	emit(osk, OSK_EVENT_DONE);
}

static void cancel(osk_keyboard *osk)
{
	if (osk->cfg.flags & OSK_FLAG_NO_CANCEL)
		return;
	osk->status = OSK_STATUS_CANCELLED;
	sound(osk, OSK_SOUND_CANCEL);
	emit(osk, OSK_EVENT_CANCEL);
}

/* Enter key, USB Return and typed '\n' */
static void enter(osk_keyboard *osk)
{
	if (multiline(osk))
		insert_char(osk, '\n');
	else
		confirm(osk);
}

/* move the caret one line up or down, keeping its x position */
static void caret_line(osk_keyboard *osk, int dir)
{
	unsigned start[OSK_MAX_CHARS + 2];
	unsigned n = osk_wrap_text(osk, start);
	unsigned line = osk_line_of(start, n, osk->caret), i, end, best;
	float x = osk_caret_x(osk, start, line, osk->caret), bx = 1e9f;

	if ((dir < 0 && line == 0) || (dir > 0 && line + 1 >= n)) {
		reject(osk);
		return;
	}
	line += dir;
	end = start[line + 1];
	/* a line ended by '\n' or a wrap cannot hold the caret at its very end */
	if (line + 1 < n)
		end--;
	best = start[line];
	for (i = start[line]; i <= end; i++) {
		float d = fabsf(osk_caret_x(osk, start, line, i) - x);
		if (d < bx) {
			bx = d;
			best = i;
		}
	}
	osk->caret = best;
	sound(osk, OSK_SOUND_SCROLL);
}

static void set_focus(osk_keyboard *osk, int key)
{
	if (key == osk->focus)
		return;
	osk->focus = key;
	if (key >= 0) {
		emit(osk, OSK_EVENT_FOCUS);
		sound(osk, OSK_SOUND_FOCUS);
	}
}

static int key_at(const osk_keyboard *osk, float x, float y)
{
	int i;

	for (i = 0; i < osk->nkeys; i++) {
		const osk_key *k = &osk->keys[i];
		if (x >= k->hx && x < k->hx + k->hw && y >= k->hy && y < k->hy + k->hh)
			return i;
	}
	return -1;
}

static int find_kind(const osk_keyboard *osk, key_kind kind)
{
	int i;

	for (i = 0; i < osk->nkeys; i++)
		if (osk->keys[i].kind == kind)
			return i;
	return -1;
}

/* 'q' (first key of the second row) on letter pages, '1' on the numpad */
static int default_key(const osk_keyboard *osk)
{
	int i;

	if (!osk->numpad)
		for (i = 0; i < osk->nkeys; i++)
			if (osk->keys[i].kind == KEY_CHAR && osk->keys[i].cy > osk->keys[0].cy)
				return i;
	return 0;
}

#define SAME_ROW	12.0f

static void move_focus(osk_keyboard *osk, int dx, int dy)
{
	const osk_key *cur, *k;
	float cx, cy, best = 1e9f, score;
	int i, found = -1;

	if (osk->focus < 0) {
		set_focus(osk, default_key(osk));
		return;
	}
	cur = &osk->keys[osk->focus];
	cx = cur->cx;
	cy = cur->cy;

	for (i = 0; i < osk->nkeys; i++) {
		if (i == osk->focus)
			continue;
		k = &osk->keys[i];
		if (dx) {
			if (fabsf(k->cy - cy) > SAME_ROW || (k->cx - cx) * dx <= 0)
				continue;
			score = fabsf(k->cx - cx);
		} else {
			if ((k->cy - cy) * dy <= SAME_ROW)
				continue;
			/* nearest row first, then the key closest to the current column */
			score = roundf(fabsf(k->cy - cy) / SAME_ROW) * 1000.0f;
			if (cx < k->hx || cx >= k->hx + k->hw)
				score += fabsf(k->cx - cx);
		}
		if (score < best) {
			best = score;
			found = i;
		}
	}

	if (found < 0 && dx) {
		/* wrap within the row */
		for (i = 0; i < osk->nkeys; i++) {
			k = &osk->keys[i];
			if (fabsf(k->cy - cy) > SAME_ROW)
				continue;
			score = k->cx * dx;	/* LEFT wraps to the rightmost key and vice versa */
			if (found < 0 || score < best) {
				best = score;
				found = i;
			}
		}
	}
	if (found >= 0)
		set_focus(osk, found);
}

static void switch_page(osk_keyboard *osk, bool numpad)
{
	key_kind tab = numpad ? KEY_TAB_NUMPAD : KEY_TAB_LETTERS;
	int i;

	if (osk->numpad == numpad)
		return;
	sound(osk, numpad ? OSK_SOUND_NUMPAD : OSK_SOUND_LETTERS);
	osk->numpad = numpad;
	osk->shift = osk->caps = osk->alt = false;
	osk_build_keys(osk);
	i = find_kind(osk, tab);
	osk->focus = i >= 0 ? i : default_key(osk);
	if (i >= 0)
		osk->keys[i].focus = 1;
}

/*-------------------------------------------------------------
  word prediction
-------------------------------------------------------------*/
/* key nearest to x in the first key row below the candidate bar */
static int key_below_bar(const osk_keyboard *osk, float x)
{
	float best = 1e9f, score;
	int i, found = default_key(osk);

	for (i = 0; i < osk->nbase; i++) {
		const osk_key *k = &osk->keys[i];
		if (k->cy <= OSK_BAR_CY + SAME_ROW)
			continue;
		score = roundf((k->cy - OSK_BAR_CY) / SAME_ROW) * 1000.0f + fabsf(k->cx - x);
		if (score < best) {
			best = score;
			found = i;
		}
	}
	return found;
}

/* re-add the bar's keys; focus stays on the same slot if possible */
static void rebuild_bar(osk_keyboard *osk)
{
	int old = osk->focus, idx;
	key_kind kind = old >= 0 ? osk->keys[old].kind : KEY_CHAR;
	float x = old >= 0 ? osk->keys[old].cx : 0;

	osk_build_bar(osk);
	if (osk->pointer_valid) {
		osk->focus = key_at(osk, osk->px, osk->py);
		return;
	}
	if (old < osk->nbase)
		return;
	if (old < osk->nkeys && osk->keys[old].kind == kind)
		idx = old;
	else if (kind == KEY_CANDIDATE && osk->keys[osk->nkeys - 1].kind == KEY_CANDIDATE)
		idx = osk->nkeys - 1;
	else
		idx = key_below_bar(osk, x);
	osk->focus = idx;
	osk->keys[idx].focus = 1;
}

static void refresh_prediction(osk_keyboard *osk, bool force)
{
	if (osk_predict_update(osk, force))
		rebuild_bar(osk);
}

/* first candidate of the page before the visible one */
static unsigned previous_page(const osk_keyboard *osk)
{
	unsigned i = osk->cand_first;
	float x = OSK_BAR_X0;

	while (i > 0) {
		float w = osk_cand_width(&osk->cand[i - 1]);
		if (x + w > OSK_BAR_X1 && i != osk->cand_first)
			break;
		x += w + OSK_BAR_GAP;
		i--;
	}
	return i;
}

static void scroll_candidates(osk_keyboard *osk, int dir)
{
	if (dir < 0 && osk->cand_first > 0)
		osk->cand_first = previous_page(osk);
	else if (dir > 0 && osk->cand_first + osk->cand_shown < osk->ncand)
		osk->cand_first += osk->cand_shown;
	else {
		reject(osk);
		return;
	}
	sound(osk, OSK_SOUND_SCROLL);
	rebuild_bar(osk);
}

static bool choose_candidate(osk_keyboard *osk, unsigned i)
{
	const osk_candidate *c = &osk->cand[i];
	unsigned start = osk->word_start, n = osk->word_len;

	if (i >= osk->ncand || osk->caret != start + n ||
	    osk->len - n + c->len > osk->max_chars) {
		reject(osk);
		return false;
	}
	memmove(&osk->text[start + c->len], &osk->text[start + n],
		(osk->len - start - n) * sizeof(uint32_t));
	memcpy(&osk->text[start], c->cp, c->len * sizeof(uint32_t));
	osk->len = osk->len - n + c->len;
	osk->caret = start + c->len;
	osk->utf8_dirty = true;

	/* followed by a space */
	if (osk->caret < osk->len && osk->text[osk->caret] == ' ')
		osk->caret++;
	else if (osk->len < osk->max_chars && osk_accepts(osk, ' '))
		put_char(osk, ' ');
	sound(osk, OSK_SOUND_CANDIDATE);
	refresh_prediction(osk, false);
	ensure_caret_visible(osk);
	return true;
}

static void toggle_prediction(osk_keyboard *osk)
{
	osk->predict = !osk->predict;
	sound(osk, osk->predict ? OSK_SOUND_PREDICT_ON : OSK_SOUND_PREDICT_OFF);
	refresh_prediction(osk, true);
}

static void toggle_shift(osk_keyboard *osk, bool *latch)
{
	*latch = !*latch;
	sound(osk, *latch ? OSK_SOUND_SHIFT_ON : OSK_SOUND_SHIFT_OFF);
}

static void toggle_more(osk_keyboard *osk)
{
	osk->alt = !osk->alt;
	sound(osk, osk->alt ? OSK_SOUND_MORE : OSK_SOUND_BACK);
}

static void move_caret(osk_keyboard *osk, unsigned to)
{
	if (to == osk->caret)
		return;
	osk->caret = to;
	sound(osk, OSK_SOUND_CARET);
}

static void press_key(osk_keyboard *osk, int idx, bool repeat)
{
	osk_key *k = &osk->keys[idx];

	if (repeat && k->kind != KEY_BACK)
		return;

	k->push = 0;
	emit(osk, OSK_EVENT_PRESS);

	switch (k->kind) {
	case KEY_CHAR:
		if (insert_char(osk, osk_key_char(osk, k)))
			osk->shift = false;
		break;
	case KEY_SPACE:
		insert_char(osk, ' ');
		break;
	case KEY_BACK:
		backspace(osk);
		break;
	case KEY_ENTER:
		enter(osk);
		break;
	case KEY_SHIFT:
		toggle_shift(osk, &osk->shift);
		break;
	case KEY_CAPS:
		toggle_shift(osk, &osk->caps);
		break;
	case KEY_ALT:
		toggle_more(osk);
		break;
	case KEY_OK:
		confirm(osk);
		break;
	case KEY_CANCEL:
		cancel(osk);
		break;
	case KEY_TAB_LETTERS:
		switch_page(osk, false);
		break;
	case KEY_TAB_NUMPAD:
		switch_page(osk, true);
		break;
	case KEY_PREDICT:
		toggle_prediction(osk);
		break;
	case KEY_SCROLL_L:
	case KEY_SCROLL_R:
		scroll_candidates(osk, k->kind == KEY_SCROLL_L ? -1 : 1);
		break;
	case KEY_CANDIDATE:
		choose_candidate(osk, k->cand);
		break;
	}
}

static bool pointer_in(const osk_keyboard *osk, float x, float y, float w, float h)
{
	return osk->px >= x && osk->px < x + w && osk->py >= y && osk->py < y + h;
}

/* text box: click places the caret, the arrows move it by a line */
static bool pointer_textbox(osk_keyboard *osk)
{
	unsigned start[OSK_MAX_CHARS + 2], n, line, i, end, best;
	float x, bx = 1e9f;

	if (!pointer_in(osk, 39, 30.33f, 556, 100))
		return false;

	n = osk_wrap_text(osk, start);
	if (n > OSK_TEXT_LINES && pointer_in(osk, 546, 46, 38, 30)) {
		caret_line(osk, -1);
		return true;
	}
	if (n > OSK_TEXT_LINES && pointer_in(osk, 546, 85, 38, 30)) {
		caret_line(osk, 1);
		return true;
	}

	line = osk->py < OSK_TEXT_Y + OSK_TEXT_LINE_H ? 0 : 1;
	line += osk->scroll;
	if (line >= n)
		line = n - 1;
	end = start[line + 1];
	if (line + 1 < n)
		end--;
	x = osk->px - OSK_TEXT_X;
	best = start[line];
	for (i = start[line]; i <= end; i++) {
		float d = fabsf(osk_caret_x(osk, start, line, i) - x);
		if (d < bx) {
			bx = d;
			best = i;
		}
	}
	osk->caret = best;
	sound(osk, OSK_SOUND_CARET_FIX);
	return true;
}

static void animate(osk_keyboard *osk)
{
	int i;

	for (i = 0; i < osk->nkeys; i++) {
		osk_key *k = &osk->keys[i];

		if (i == osk->focus)
			k->focus = fminf(k->focus + 1.0f / OSK_FOCUS_IN_FRAMES, 1);
		else
			k->focus = fmaxf(k->focus - 1.0f / OSK_FOCUS_OUT_FRAMES, 0);
		if (k->push >= 0 && ++k->push > OSK_PUSH_FRAMES)
			k->push = -1;
	}
}

/*-------------------------------------------------------------
  public API
-------------------------------------------------------------*/
osk_keyboard *OSK_Create(const osk_config *cfg)
{
	osk_keyboard *osk;
	osk_config defaults;

	if (!cfg) {
		OSK_DefaultConfig(&defaults);
		cfg = &defaults;
	}
	if (!osk_backend_init())
		return NULL;

	osk = calloc(1, sizeof(*osk));
	if (!osk)
		return NULL;

	osk->cfg = *cfg;
	osk->theme = cfg->theme ? *cfg->theme : default_theme;
	osk->max_chars = cfg->max_chars && cfg->max_chars <= OSK_MAX_CHARS ? cfg->max_chars : OSK_MAX_CHARS;
	if (!osk->cfg.screen_width)
		osk->cfg.screen_width = 640;
	if (!osk->cfg.screen_height)
		osk->cfg.screen_height = 480;

	/* The virtual canvas is always 480 lines tall and as wide as needed for
	   square pixels on the display; the design area is centered in it. */
	osk->vh = OSK_DESIGN_H;
	osk->vw = (cfg->flags & OSK_FLAG_WIDESCREEN) ? OSK_DESIGN_W * 4.0f / 3.0f : OSK_DESIGN_W;
	osk->ox = (osk->vw - OSK_DESIGN_W) / 2;
	osk->oy = 0;

	osk->numpad = cfg->layout == OSK_LAYOUT_NUMPAD;
	osk->letters = osk->numpad ? OSK_LAYOUT_QWERTY : cfg->layout;
	if (cfg->dictionary && !(cfg->flags & OSK_FLAG_PASSWORD))
		osk_dict_parse(&osk->dict, cfg->dictionary);
	osk->predict = !(cfg->flags & OSK_FLAG_PREDICT_OFF);
	osk_build_keys(osk);
	osk->focus = default_key(osk);
	osk->keys[osk->focus].focus = 1;

	OSK_SetText(osk, cfg->initial);
	sound(osk, OSK_SOUND_OPEN);
	return osk;
}

void OSK_Destroy(osk_keyboard *osk)
{
	free(osk);
}

void OSK_SetText(osk_keyboard *osk, const char *utf8)
{
	uint32_t cp;
	size_t n;

	osk->len = 0;
	while (utf8 && (n = OSK_UTF8Decode(utf8, &cp)) != 0) {
		utf8 += n;
		if (cp == '\r')
			continue;
		if (cp == 0xA0 || (cp == '\n' && !multiline(osk)))
			cp = ' ';
		if (osk->len >= osk->max_chars)
			break;
		if (cp != '\n' && !OSK_CanDisplay(cp))
			cp = '?';
		osk->text[osk->len++] = cp;
	}
	osk->caret = osk->len;
	osk->scroll = 0;
	osk->utf8_dirty = true;
	ensure_caret_visible(osk);
	refresh_prediction(osk, true);
}

const char *OSK_GetText(osk_keyboard *osk)
{
	unsigned i;
	size_t pos = 0;

	if (osk->utf8_dirty) {
		for (i = 0; i < osk->len; i++)
			pos += OSK_UTF8Encode(osk->text[i], &osk->utf8[pos]);
		osk->utf8[pos] = 0;
		osk->utf8_dirty = false;
	}
	return osk->utf8;
}

size_t OSK_CopyText(osk_keyboard *osk, char *buf, size_t size)
{
	unsigned i;
	size_t pos = 0, n;
	char tmp[4];

	if (!buf || !size)
		return 0;
	for (i = 0; i < osk->len; i++) {
		n = OSK_UTF8Encode(osk->text[i], tmp);
		if (pos + n >= size)
			break;
		memcpy(&buf[pos], tmp, n);
		pos += n;
	}
	buf[pos] = 0;
	return pos;
}

unsigned OSK_GetLength(const osk_keyboard *osk)
{
	return osk->len;
}

osk_status OSK_GetStatus(const osk_keyboard *osk)
{
	return osk->status;
}

void OSK_Resume(osk_keyboard *osk)
{
	osk->status = OSK_STATUS_EDITING;
}

#define REPEATABLE	(OSK_BTN_SELECT | OSK_BTN_BACKSPACE | OSK_BTN_DELETE | \
			 OSK_BTN_UP | OSK_BTN_DOWN | OSK_BTN_LEFT | OSK_BTN_RIGHT | \
			 OSK_BTN_CARET_LEFT | OSK_BTN_CARET_RIGHT)

osk_status OSK_Update(osk_keyboard *osk, const osk_input *in)
{
	u32 fire = 0, repeated = 0;
	unsigned i;
	osk_input none;

	if (osk->status != OSK_STATUS_EDITING)
		return osk->status;
	if (!in) {
		memset(&none, 0, sizeof(none));
		in = &none;
	}

	osk->frame++;

	/* pointer */
	if (in->pointer_valid) {
		float px = in->pointer_x * osk->vw / osk->cfg.screen_width - osk->ox;
		float py = in->pointer_y * osk->vh / osk->cfg.screen_height - osk->oy;
		bool moved = !osk->pointer_valid ||
			     fabsf(px - osk->last_px) > 1.5f || fabsf(py - osk->last_py) > 1.5f;

		osk->px = px;
		osk->py = py;
		if (moved) {
			set_focus(osk, key_at(osk, px, py));
			osk->last_px = px;
			osk->last_py = py;
		}
	}
	osk->pointer_valid = in->pointer_valid;

	/* auto repeat */
	for (i = 0; i < 16; i++) {
		u32 bit = 1u << i;
		if (in->down & bit) {
			osk->repeat[i] = 0;
			fire |= bit;
		} else if (in->held & bit) {
			if (osk->repeat[i] < 0xFFFF)
				osk->repeat[i]++;
			if ((REPEATABLE & bit) && osk->repeat[i] >= OSK_REPEAT_DELAY &&
			    (osk->repeat[i] - OSK_REPEAT_DELAY) % OSK_REPEAT_RATE == 0) {
				fire |= bit;
				repeated |= bit;
			}
		} else {
			osk->repeat[i] = 0;
		}
	}

	for (i = 0; i < in->text_count && i < OSK_INPUT_MAX_TEXT && osk->status == OSK_STATUS_EDITING; i++) {
		if (in->text[i] == '\n' || in->text[i] == '\r')
			enter(osk);
		else
			insert_char(osk, in->text[i]);
	}

	if (fire & OSK_BTN_SELECT) {
		bool rep = (repeated & OSK_BTN_SELECT) != 0;
		if (!rep && in->pointer_valid && osk->focus < 0 && pointer_textbox(osk))
			;
		else if (osk->focus >= 0)
			press_key(osk, osk->focus, rep);
	}
	if (fire & OSK_BTN_BACKSPACE)	backspace(osk);
	if (fire & OSK_BTN_DELETE)	delete_char(osk);
	if ((fire & OSK_BTN_CARET_LEFT) && osk->caret)
		move_caret(osk, osk->caret - 1);
	if ((fire & OSK_BTN_CARET_RIGHT) && osk->caret < osk->len)
		move_caret(osk, osk->caret + 1);
	if (fire & OSK_BTN_CARET_HOME)	move_caret(osk, 0);
	if (fire & OSK_BTN_CARET_END)	move_caret(osk, osk->len);
	if (fire & OSK_BTN_SHIFT)	toggle_shift(osk, &osk->shift);
	if ((fire & OSK_BTN_ALT) && !osk->numpad)
		toggle_more(osk);
	if (fire & OSK_BTN_SPACE)	insert_char(osk, ' ');
	if (fire & OSK_BTN_UP)		move_focus(osk, 0, -1);
	if (fire & OSK_BTN_DOWN)	move_focus(osk, 0, 1);
	if (fire & OSK_BTN_LEFT)	move_focus(osk, -1, 0);
	if (fire & OSK_BTN_RIGHT)	move_focus(osk, 1, 0);

	if (osk->status == OSK_STATUS_EDITING) {
		if (fire & OSK_BTN_CONFIRM)
			confirm(osk);
		else if (fire & OSK_BTN_CANCEL)
			cancel(osk);
	}

	refresh_prediction(osk, false);
	animate(osk);
	ensure_caret_visible(osk);
	return osk->status;
}

void OSK_SetPrediction(osk_keyboard *osk, bool on)
{
	if (osk->predict != on)
		toggle_prediction(osk);
}

bool OSK_GetPrediction(const osk_keyboard *osk)
{
	return osk->dict.ok && osk->predict;
}

unsigned OSK_GetCandidateCount(const osk_keyboard *osk)
{
	return osk->ncand;
}

size_t OSK_CopyCandidate(const osk_keyboard *osk, unsigned i, char *buf, size_t size)
{
	size_t pos = 0, n;
	unsigned j;
	char tmp[4];

	if (!buf || !size)
		return 0;
	buf[0] = 0;
	if (i >= osk->ncand)
		return 0;
	for (j = 0; j < osk->cand[i].len; j++) {
		n = OSK_UTF8Encode(osk->cand[i].cp[j], tmp);
		if (pos + n >= size)
			break;
		memcpy(&buf[pos], tmp, n);
		pos += n;
	}
	buf[pos] = 0;
	return pos;
}

bool OSK_ChooseCandidate(osk_keyboard *osk, unsigned i)
{
	if (i >= osk->ncand) {
		reject(osk);
		return false;
	}
	return choose_candidate(osk, i);
}
