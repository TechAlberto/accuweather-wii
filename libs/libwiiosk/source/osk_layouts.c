/*-------------------------------------------------------------
osk_layouts.c -- key arrangements and geometry
-------------------------------------------------------------*/
#include <string.h>

#include "osk_internal.h"

/* A letter layout fills up to 50 key slots: rows of 11, 10, 10 and 10
   keys, then six (or nine) keys around the space bar. Each table is
   four UTF-8 strings of 50 characters, one per state: default, Caps,
   Shift, Shift+Caps. A space marks a slot without a key. */
#define NSLOTS		50

typedef struct {
	const char *state[4];
} letter_table;

static const letter_table table_qwerty = { {
	"1234567890-" "qwertyuiop" "asdfghjkl:" "zxcvbnm,.=" "   " "[]'`/@",
	"1234567890-" "QWERTYUIOP" "ASDFGHJKL:" "ZXCVBNM,.=" "   " "[]'`/@",
	"!\\#$%^&*()_" "QWERTYUIOP" "ASDFGHJKL;" "ZXCVBNM<>+" "   " "{}\"?~|",
	"!\\#$%^&*()_" "qwertyuiop" "asdfghjkl;" "zxcvbnm<>+" "   " "{}\"?~|",
} };

static const letter_table table_qwertz = { {
	"1234567890\xC3\x9F" "qwertzuiop" "asdfghjkl\xC3\xBC" "yxcvbnm\xC3\xB6\xC3\xA4+" "[]\\" "#@\xC2\xB4,.-",
	"1234567890\xC3\x9F" "QWERTZUIOP" "ASDFGHJKL\xC3\x9C" "YXCVBNM\xC3\x96\xC3\x84*" "{}~" "'\xE2\x82\xAC`;:_",
	"!\"\xC2\xA7$%&/()=?" "QWERTZUIOP" "ASDFGHJKL\xC3\x9C" "YXCVBNM\xC3\x96\xC3\x84*" "<>~" "'\xE2\x82\xAC`;:_",
	"!\"\xC2\xA7$%&/()=?" "qwertzuiop" "asdfghjkl\xC3\xBC" "yxcvbnm\xC3\xB6\xC3\xA4+" "<>~" "#@\xC2\xB4,.-",
} };

static const letter_table table_azerty = { {
	"&\xC3\xA9\"'(-\xC3\xA8_\xC3\xA7\xC3\xA0)" "azertyuiop" "qsdfghjklm" "wxcvbn,.;:"
		"\xC5\x93^\xC2\xA8" "[]\xC3\xB9@!?",
	"1234567890\xC2\xB0" "AZERTYUIOP" "QSDFGHJKLM" "WXCVBN+-*/"
		"\xC5\x92\xC3\x87\\" "[]=%<>",
	"\xC3\x80\xC3\x82\xC3\x88\xC3\x89\xC3\x8A\xC3\x8B\xC3\x8E\xC3\x8F\xC3\x94\xC3\x99\xC3\x9B"
		"AZERTYUIOP" "QSDFGHJKLM" "WXCVBN~#|\xC2\xB5"
		"\xC5\x92\xC3\x87\\" "{}\xC2\xA7\xE2\x82\xAC\xC2\xA3$",
	"\xC3\xA0\xC3\xA5\xC3\xA8\xC3\xA9\xC3\xAA\xC3\xAB\xC3\xAE\xC3\xAF\xC3\xB4\xC3\xB9\xC3\xBB"
		"azertyuiop" "qsdfghjklm" "wxcvbn~#|\xC2\xB5"
		"\xC5\x93^\xC2\xA8" "{}\xC2\xA7\xE2\x82\xAC\xC2\xA3$",
} };

static const letter_table table_uk = { {
	"1234567890-" "qwertyuiop" "asdfghjkl;" "zxcvbnm,.=" " \xC2\xA5\xE2\x82\xAC" "[]\\/'#",
	"1234567890-" "QWERTYUIOP" "ASDFGHJKL;" "ZXCVBNM,.=" " \xC2\xA5\xE2\x82\xAC" "[]\\/'#",
	"!\"\xC2\xA3$%^&*()_" "QWERTYUIOP" "ASDFGHJKL:" "ZXCVBNM<>+" " \xC2\xA6`" "{}|?@~",
	"!\"\xC2\xA3$%^&*()_" "qwertyuiop" "asdfghjkl:" "zxcvbnm<>+" " \xC2\xA6`" "{}|?@~",
} };

static const letter_table table_it = { {
	"1234567890'" "qwertyuiop" "asdfghjkl+" "zxcvbnm,.-"
		"\xC3\xA0\xC3\xA8\xC3\xA9" "\xC3\xAC@#\xE2\x82\xAC\xC3\xB2\xC3\xB9",
	"1234567890'" "QWERTYUIOP" "ASDFGHJKL+" "ZXCVBNM,.-"
		"\xC3\x80\xC3\x88\xC3\x89" "\xC3\x8C@#\xE2\x82\xAC\xC3\x92\xC3\x99",
	"!\"\xC2\xA3$%&/()=?" "QWERTYUIOP" "ASDFGHJKL*" "ZXCVBNM;:_" "^<>" "\xC2\xB0[]~\\|",
	"!\"\xC2\xA3$%&/()=?" "qwertyuiop" "asdfghjkl*" "zxcvbnm;:_" "^<>" "\xC2\xB0[]~\\|",
} };

static const letter_table table_es = { {
	"1234567890\xC2\xA1" "qwertyuiop" "asdfghjkl\xC3\xB1" "zxcvbnm,.-" " \xC2\xB7\xC3\xA7" "()@\xC2\xB4`+",
	"1234567890\xC2\xA1" "QWERTYUIOP" "ASDFGHJKL\xC3\x91" "ZXCVBNM\xC2\xBA\xC2\xAA_"
		" \xC2\xAB\xC2\xBB" "<>\xE2\x82\xAC#\\'",
	"!\"$%&/{}=?\xC2\xBF" "QWERTYUIOP" "ASDFGHJKL\xC3\x91" "ZXCVBNM;:_" " |\xC3\x87" "[]~\xC2\xA8^*",
	"!\"$%&/{}=?\xC2\xBF" "qwertyuiop" "asdfghjkl\xC3\xB1" "zxcvbnm;:_" " |\xC3\x87" "[]~\xC2\xA8^*",
} };

static const letter_table table_nl = { {
	"1234567890-" "qwertyuiop" "asdfghjkl:" "zxcvbnm,./" "  `" "[]\\'=\xE2\x82\xAC",
	"1234567890-" "QWERTYUIOP" "ASDFGHJKL:" "ZXCVBNM,./" "  `" "[]\\'=\xE2\x82\xAC",
	"!@#$%^&*()_" "QWERTYUIOP" "ASDFGHJKL;" "ZXCVBNM<>?" "  ~" "{}|\"+\xE2\x82\xAC",
	"!@#$%^&*()_" "qwertyuiop" "asdfghjkl;" "zxcvbnm<>?" "  ~" "{}|\"+\xE2\x82\xAC",
} };

/* "More" page, shared by all letter layouts and filled into the visible
   slots in reading order: accented letters and extra symbols. The last
   three characters are only used by layouts with 50 slots. */
static const char alt_normal[] =
	"\xC3\xA0\xC3\xA1\xC3\xA2\xC3\xA3\xC3\xA4\xC3\xA5\xC3\xA6\xC3\xA7\xC3\xA8\xC3\xA9\xC3\xAA\xC3\xAB\xE2\x82\xAC"
	"\xC3\xAC\xC3\xAD\xC3\xAE\xC3\xAF\xC3\xB1\xC3\xB2\xC3\xB3\xC3\xB4\xC3\xB5\xC3\xB6\xC3\xB8\xC3\xB9\xC3\xBA"
	"\xC3\xBB\xC3\xBC\xC3\xBD\xC3\xBF\xC3\x9F\xC2\xA1\xC2\xBF\xC2\xAB\xC2\xBB\xC2\xB0\xC2\xA3"
	"@{}[]<>|\\~"
	"\xC2\xA9\xC2\xAE\xC2\xB6";
static const char alt_shifted[] =
	"\xC3\x80\xC3\x81\xC3\x82\xC3\x83\xC3\x84\xC3\x85\xC3\x86\xC3\x87\xC3\x88\xC3\x89\xC3\x8A\xC3\x8B\xE2\x82\xAC"
	"\xC3\x8C\xC3\x8D\xC3\x8E\xC3\x8F\xC3\x91\xC3\x92\xC3\x93\xC3\x94\xC3\x95\xC3\x96\xC3\x98\xC3\x99\xC3\x9A"
	"\xC3\x9B\xC3\x9C\xC3\x9D\xC3\xBF\xC3\x9F\xC2\xA1\xC2\xBF\xC2\xAB\xC2\xBB\xC2\xB0\xC2\xA3"
	"\xC2\xA2\xC2\xA5\xC2\xA7\xC2\xB5\xC2\xB1\xC3\xB7\xC3\x97\xC2\xB2\xC2\xBA\xC2\xAA"
	"\xC2\xB9\xC2\xB3\xC2\xBD";

/* key centers of the letter slots */
static void slot_center(int i, float *x, float *y)
{
	static const float last[4] = { 443.33f, 486.33f, 528.83f, 572.33f };

	if (i < 11) {
		*x = 67 + 43 * i;
		*y = 190;
	} else if (i < 21) {
		*x = 88 + 43 * (i - 11);
		*y = 225;
	} else if (i < 31) {
		*x = 153 + 43 * (i - 21);
		*y = 260;
	} else if (i < 41) {
		*x = 166 + 43 * (i - 31);
		*y = 295;
	} else {
		*x = i < 46 ? 67 + 43 * (i - 41) : last[i - 46];
		*y = 330;
	}
}

static osk_key *add_key(osk_keyboard *osk, key_kind kind, key_look look,
			float cx, float cy, float w, float h, float hw, float hh)
{
	osk_key *k;

	if (osk->nkeys >= OSK_MAX_KEYS)
		return NULL;
	k = &osk->keys[osk->nkeys++];
	memset(k, 0, sizeof(*k));
	k->kind = kind;
	k->look = look;
	k->cx = cx;
	k->cy = cy;
	k->w = w;
	k->h = h;
	k->hx = cx - hw / 2;
	k->hy = cy - hh / 2;
	k->hw = hw;
	k->hh = hh;
	k->push = -1;
	return k;
}

static bool is_letter(uint32_t cp)
{
	if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z'))
		return true;
	return cp >= 0xC0 && cp <= 0xFF && cp != 0xD7 && cp != 0xF7 && cp != 0xDF && cp != 0xFF;
}

static void build_letters(osk_keyboard *osk, const letter_table *t)
{
	const char *s[4], *an = alt_normal, *as = alt_shifted;
	uint32_t cp[4], a, b;
	int i, j;
	bool enter = !(osk->cfg.flags & OSK_FLAG_NO_ENTER);

	for (j = 0; j < 4; j++)
		s[j] = t->state[j];

	for (i = 0; i < NSLOTS; i++) {
		float x, y;
		osk_key *k;

		for (j = 0; j < 4; j++)
			s[j] += OSK_UTF8Decode(s[j], &cp[j]);
		if (cp[0] == ' ')
			continue;

		slot_center(i, &x, &y);
		k = add_key(osk, KEY_CHAR, LOOK_CHAR, x, y, 43, 35, 43, 35);
		if (!k)
			break;
		memcpy(k->cp[0], cp, sizeof(cp));

		an += OSK_UTF8Decode(an, &a);
		as += OSK_UTF8Decode(as, &b);
		k->cp[1][0] = a;
		k->cp[1][1] = is_letter(a) ? b : a;
		k->cp[1][2] = b;
		k->cp[1][3] = is_letter(a) ? a : b;
	}

	add_key(osk, KEY_BACK, LOOK_SPECIAL, 556, 190, 70, 32, 75, 35);
	if (enter)
		add_key(osk, KEY_ENTER, LOOK_SPECIAL, 545, 225, 92, 32, 93, 35);
	add_key(osk, KEY_CAPS, LOOK_SPECIAL, 88, 260, 84, 32, 85, 35);
	add_key(osk, KEY_SHIFT, LOOK_SPECIAL, 93, 295, 94, 32, 96, 35);
	add_key(osk, KEY_SPACE, LOOK_SPACE, 341, 330, 156, 32, 161, 35);
	add_key(osk, KEY_ALT, LOOK_MORE, 506, 369, 174, 40, 174, 40);
	if (osk->dict.ok)
		add_key(osk, KEY_PREDICT, LOOK_MORE, 133, 370, 174, 40, 174, 40);
}

static void build_numpad(osk_keyboard *osk)
{
	static const char digits[] = "123456789-0.";
	int i;

	for (i = 0; i < 12; i++) {
		osk_key *k = add_key(osk, KEY_CHAR, LOOK_PAD, 146 + 116 * (i % 3), 199 + 53 * (i / 3),
				     116, 54, 115, 53);
		if (k)
			k->cp[0][0] = k->cp[0][1] = k->cp[0][2] = k->cp[0][3] = digits[i];
	}
	add_key(osk, KEY_BACK, LOOK_PAD, 499, 199, 116, 54, 115, 53);
	if (!(osk->cfg.flags & OSK_FLAG_NO_ENTER))
		add_key(osk, KEY_ENTER, LOOK_PAD, 499, 252, 116, 54, 115, 53);
	{
		osk_key *k = add_key(osk, KEY_CHAR, LOOK_PAD, 499, 305, 116, 54, 115, 53);
		if (k)
			k->cp[0][0] = k->cp[0][1] = k->cp[0][2] = k->cp[0][3] = ',';
	}
}

void osk_build_keys(osk_keyboard *osk)
{
	osk->nkeys = 0;
	if (osk->numpad) {
		build_numpad(osk);
	} else {
		switch (osk->letters) {
		case OSK_LAYOUT_QWERTZ:		build_letters(osk, &table_qwertz); break;
		case OSK_LAYOUT_AZERTY:		build_letters(osk, &table_azerty); break;
		case OSK_LAYOUT_QWERTY_UK:	build_letters(osk, &table_uk); break;
		case OSK_LAYOUT_QWERTY_IT:	build_letters(osk, &table_it); break;
		case OSK_LAYOUT_QWERTY_ES:	build_letters(osk, &table_es); break;
		case OSK_LAYOUT_QWERTY_NL:	build_letters(osk, &table_nl); break;
		default:			build_letters(osk, &table_qwerty); break;
		}
	}

	/* toolbar */
	if (!(osk->cfg.flags & OSK_FLAG_NO_CANCEL))
		add_key(osk, KEY_CANCEL, LOOK_PILL, 125, 425, 172, 52, 172, 48);
	if (!(osk->cfg.flags & OSK_FLAG_LOCK_LAYOUT)) {
		add_key(osk, KEY_TAB_LETTERS, LOOK_TAB, 287, 424, 60, 38, 60, 40);
		add_key(osk, KEY_TAB_NUMPAD, LOOK_TAB, 352, 424, 58, 38, 60, 40);
	}
	add_key(osk, KEY_OK, LOOK_PILL, 518, 425, 172, 52, 172, 48);

	osk->nbase = osk->nkeys;
	osk_build_bar(osk);
}

/*-------------------------------------------------------------
  candidate bar
-------------------------------------------------------------*/
const osk_font_size osk_cand_font = { 23.4f, 24.3f, 0 };

/* text width, squeezed to fit the bar */
float osk_cand_width(const osk_candidate *c)
{
	float w = osk_text_width(c->cp, c->len, &osk_cand_font);
	return w < OSK_BAR_X1 - OSK_BAR_X0 ? w : OSK_BAR_X1 - OSK_BAR_X0;
}

/* the scroll arrows, then the candidates of the page starting at
   cand_first that fit */
void osk_build_bar(osk_keyboard *osk)
{
	float x = OSK_BAR_X0;
	unsigned i;

	osk->nkeys = osk->nbase;
	osk->cand_shown = 0;
	if (!osk_predict_active(osk))
		return;
	add_key(osk, KEY_SCROLL_L, LOOK_SCROLL, 458, OSK_BAR_CY, 36, 32, 42, 36);
	add_key(osk, KEY_SCROLL_R, LOOK_SCROLL, 500, OSK_BAR_CY, 36, 32, 42, 36);
	for (i = osk->cand_first; i < osk->ncand; i++) {
		float w = osk_cand_width(&osk->cand[i]);
		osk_key *k;

		if (x + w > OSK_BAR_X1 && i > osk->cand_first)
			break;
		k = add_key(osk, KEY_CANDIDATE, LOOK_CANDIDATE, x + w / 2, OSK_BAR_CY, w + 12, 30,
			    w + OSK_BAR_GAP, 36);
		if (!k)
			break;
		k->cand = i;
		x += w + OSK_BAR_GAP;
		osk->cand_shown++;
	}
}

uint32_t osk_key_char(const osk_keyboard *osk, const osk_key *k)
{
	int page = osk->alt && !osk->numpad ? 1 : 0;

	if (k->kind == KEY_SPACE)
		return ' ';
	if (k->kind != KEY_CHAR)
		return 0;
	return k->cp[page][(osk->caps ? 1 : 0) | (osk->shift ? 2 : 0)];
}

const char *osk_key_label(const osk_keyboard *osk, const osk_key *k)
{
	switch (k->kind) {
	case KEY_SHIFT:		return "Shift";
	case KEY_CAPS:		return "Caps";
	case KEY_SPACE:		return "Space";
	case KEY_ALT:		return osk->alt ? "Back" : "More";
	case KEY_OK:		return "OK";
	case KEY_CANCEL:	return "Quit";
	case KEY_PREDICT:	return osk->dict.name;
	default:		return NULL;
	}
}

bool osk_key_latched(const osk_keyboard *osk, const osk_key *k)
{
	switch (k->kind) {
	case KEY_SHIFT:		return osk->shift;
	case KEY_CAPS:		return osk->caps;
	case KEY_TAB_LETTERS:	return !osk->numpad;
	case KEY_TAB_NUMPAD:	return osk->numpad;
	case KEY_PREDICT:	return osk->predict;
	default:		return false;
	}
}
