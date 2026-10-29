/*-------------------------------------------------------------
osk_predict.c -- word prediction from eZiText dictionaries

A .zsd holds a table of contents of 6 byte records (BE24 size,
BE24 offset) from offset 4; record 0 directly follows the table.
Record 0: u16, u16BE region size, then region size / 3 u16BE code
points (the character set), then the word graph. Record 4 is the
blocklist; its size field is an entry count, each entry a length
prefixed Latin-1 string.

The graph is a DAWG of variable length nodes. The high nibble of a
node's first byte selects its flags from node_flags[], the low
nibble is a character index (15: the next byte holds index - 15).
Child and sibling links are either implicit (the linked node follows
directly) or explicit forward pointers relative to the node: 2 bytes
below 0x8000, else 3 bytes with the top bit set and a 0x8000 bias.
Words come out of a depth-first walk roughly in order of frequency,
so that order is used as the ranking. The same letter can occur many
times in one sibling chain, so every match is followed.

A .znd is u32BE count, count u32BE offsets, then NUL terminated
UTF-16BE strings.

All reads are bounds checked and every walk has a step budget, so a
damaged file loaded from SD cannot hang or crash the keyboard.
-------------------------------------------------------------*/
#include <stdlib.h>
#include <string.h>

#include "osk_internal.h"

#define F_CHILD			0x01
#define F_CHILD_EXPLICIT	0x02
#define F_SIBLING		0x04
#define F_SIBLING_EXPLICIT	0x08
#define F_TERMINAL		0x10

static const u8 node_flags[16] = {
	0x10, 0x01, 0x03, 0x14, 0x05, 0x07, 0x1C, 0x0D,
	0x0F, 0x11, 0x13, 0x15, 0x17, 0x1D, 0x1F, 0x35,
};

#define NONE			0xFFFFFFFFu
#define STEP_BUDGET		400000

typedef struct {
	const osk_predictor *p;
	unsigned steps;
} walker;

static unsigned rd8(const osk_predictor *p, u32 o)
{
	return o < p->zsd_size ? p->zsd[o] : 0;
}

static u32 be16(const u8 *d, u32 o)
{
	return d[o] << 8 | d[o + 1];
}

static u32 be24(const u8 *d, u32 o)
{
	return d[o] << 16 | d[o + 1] << 8 | d[o + 2];
}

static u32 be32(const u8 *d, u32 o)
{
	return (u32)d[o] << 24 | be24(d, o + 1);
}

/*-------------------------------------------------------------
  graph nodes
-------------------------------------------------------------*/
static unsigned flags_of(const walker *w, u32 o)
{
	return node_flags[rd8(w->p, o) >> 4];
}

static unsigned char_width(const walker *w, u32 o)
{
	return (rd8(w->p, o) & 0xF) == 0xF ? 2 : 1;
}

static unsigned char_index(const walker *w, u32 o)
{
	unsigned b = rd8(w->p, o) & 0xF;
	return b == 0xF ? rd8(w->p, o + 1) + 15 : b;
}

static unsigned ptr_width(const walker *w, u32 o)
{
	return (rd8(w->p, o) & 0x80) ? 3 : 2;
}

static u32 read_ptr(const walker *w, u32 o)
{
	unsigned b = rd8(w->p, o);

	if (!(b & 0x80))
		return b << 8 | rd8(w->p, o + 1);
	return ((b & 0x7F) << 16) + (rd8(w->p, o + 1) << 8) + 0x8000 + rd8(w->p, o + 2);
}

static u32 checked(const walker *w, u32 node, u32 dist)
{
	u32 o = node + dist;
	return dist && o > node && o < w->p->zsd_size ? o : NONE;
}

static u32 first_child(walker *w, u32 o)
{
	unsigned f = flags_of(w, o), cw = char_width(w, o), dist;

	if (!(f & F_CHILD))
		return NONE;
	if (f & F_CHILD_EXPLICIT)
		return checked(w, o, read_ptr(w, o + cw));
	dist = cw;
	if (f & F_SIBLING_EXPLICIT)
		dist += ptr_width(w, o + cw);
	return checked(w, o, dist);
}

static u32 next_sibling(walker *w, u32 o)
{
	unsigned f = flags_of(w, o);
	u32 cur = o;
	int depth = 0;

	if (!(f & F_SIBLING))
		return NONE;
	if (f & F_SIBLING_EXPLICIT) {
		u32 po = o + char_width(w, o);
		if (f & F_CHILD_EXPLICIT)
			po += ptr_width(w, po);
		return checked(w, o, read_ptr(w, po));
	}
	/* implicit: the sibling follows this node's subtree */
	do {
		unsigned cf = flags_of(w, cur);

		if (++w->steps > STEP_BUDGET || cur >= w->p->zsd_size)
			return NONE;
		cur += char_width(w, cur);
		if (cf & F_CHILD) {
			if (cf & F_CHILD_EXPLICIT)
				cur += ptr_width(w, cur);
			else
				depth++;
		}
		if (!(cf & F_SIBLING)) {
			depth--;
		} else if (cf & F_SIBLING_EXPLICIT) {
			cur += ptr_width(w, cur);
			depth--;
		}
	} while (depth > 0);
	return checked(w, o, cur - o);
}

/*-------------------------------------------------------------
  dictionaries
-------------------------------------------------------------*/
/* unpacked built-in dictionaries */
#define MAX_UNPACKED	16

static struct {
	const osk_dictionary *dict;
	u8 *data;
} unpacked[MAX_UNPACKED];

static const u8 *unpack_dict(const osk_dictionary *d)
{
	u32 size;
	u8 *data;
	int i, free_slot = -1;

	for (i = 0; i < MAX_UNPACKED; i++) {
		if (unpacked[i].dict == d)
			return unpacked[i].data;
		if (!unpacked[i].dict && free_slot < 0)
			free_slot = i;
	}
	if (free_slot < 0)
		return NULL;
	data = osk_unpack(d->packed, d->packed_size, &size);
	if (data && size != d->zsd_size + d->znd_size) {
		free(data);
		data = NULL;
	}
	if (data) {
		unpacked[free_slot].dict = d;
		unpacked[free_slot].data = data;
	}
	return data;
}

void OSK_FreeDictionaries(void)
{
	int i;

	for (i = 0; i < MAX_UNPACKED; i++) {
		free(unpacked[i].data);
		unpacked[i].dict = NULL;
		unpacked[i].data = NULL;
	}
}

bool osk_dict_parse(osk_predictor *p, const osk_dictionary *d)
{
	const u8 *z, *znd;
	u32 base, off0, size0, off4, cnt4, region, o, i;

	memset(p, 0, sizeof(*p));
	if (!d)
		return false;
	z = d->zsd;
	znd = d->znd;
	if (d->packed) {
		z = unpack_dict(d);
		znd = z && d->znd_size ? z + d->zsd_size : NULL;
	}
	if (!z || d->zsd_size < 34 || d->zsd_size > 0xFFFFFF)
		return false;
	base = be24(z, 7);
	if (base < 34 || base > d->zsd_size)
		return false;
	size0 = be24(z, 4);
	off0 = be24(z, 7);
	cnt4 = be24(z, 4 + 4 * 6);
	off4 = be24(z, 7 + 4 * 6);
	if (off0 + 4 > d->zsd_size || size0 > d->zsd_size - off0)
		return false;

	region = be16(z, off0 + 2);
	p->zsd = z;
	p->zsd_size = d->zsd_size;
	p->charset = off0 + 4;
	p->nchars = region / 3;
	p->graph = off0 + 4 + region;
	if (!p->nchars || p->graph >= p->zsd_size)
		return false;

	/* blocklist: keep only the entries that lie inside the file */
	o = off4;
	for (i = 0; i < cnt4 && o < d->zsd_size && o + 1 + z[o] <= d->zsd_size; i++)
		o += 1 + z[o];
	p->block = off4;
	p->nblock = i;

	if (znd && d->znd_size >= 4) {
		u32 n = be32(znd, 0);
		if (n <= (d->znd_size - 4) / 4) {
			p->znd = znd;
			p->znd_size = d->znd_size;
			p->nznd = n;
		}
	}
	p->name = d->name ? d->name : "";
	p->ok = true;
	return true;
}

bool OSK_DictionaryFromMemory(osk_dictionary *d, const char *name,
			      const void *zsd, size_t zsd_size,
			      const void *znd, size_t znd_size)
{
	osk_predictor p;

	d->name = name;
	d->zsd = zsd;
	d->zsd_size = zsd_size;
	d->znd = znd;
	d->znd_size = znd ? znd_size : 0;
	d->packed = NULL;
	d->packed_size = 0;
	return osk_dict_parse(&p, d);
}

/*-------------------------------------------------------------
  letters
-------------------------------------------------------------*/
static uint32_t lower(uint32_t c)
{
	if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7))
		return c + 32;
	if (c == 0x152)
		return 0x153;
	return c;
}

static uint32_t upper(uint32_t c)
{
	if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7))
		return c - 32;
	if (c == 0x153)
		return 0x152;
	return c;
}

static bool is_upper(uint32_t c)
{
	return lower(c) != c;
}

/* accented Latin-1 lower case letter -> its base letter */
static uint32_t fold(uint32_t c)
{
	static const char base[] = "aaaaaa\0ceeeeiiii\0nooooo\0ouuuuy\0y";

	if (c >= 0xE0 && c <= 0xFF && base[c - 0xE0])
		return (u8)base[c - 0xE0];
	return c;
}

static bool is_letter(uint32_t c)
{
	c = lower(c);
	return (c >= 'a' && c <= 'z') || (c >= 0xDF && c <= 0xFF && c != 0xF7) || c == 0x153;
}

bool osk_is_word_char(uint32_t c)
{
	return is_letter(c) || c == '\'' || c == '-';
}

/* A typed base letter matches all its accented forms, a typed accented
   letter only itself. */
static bool matches(uint32_t typed, uint32_t dict)
{
	typed = lower(typed);
	return typed == dict || (fold(typed) == typed && fold(dict) == typed);
}

/*-------------------------------------------------------------
  candidates
-------------------------------------------------------------*/
typedef struct {
	osk_keyboard *osk;
	const uint32_t *typed;
	unsigned n;
	bool caps_all, caps_first;
	unsigned room;			/* code points that may replace the word */
	unsigned limit;			/* stop at this many candidates */
	bool strict;			/* no accent folding */
} search;

static bool blocked(const osk_predictor *p, const uint32_t *w, unsigned n)
{
	u32 o = p->block;
	unsigned i, j;

	if (n >= 6 && w[0] == 'z' && w[1] == 'i' && w[2] == 'c' && w[3] == 'o' &&
	    w[4] == 'r' && w[5] == 'p')
		return true;	/* the build watermark */
	for (i = 0; i < p->nblock; i++) {
		unsigned len = rd8(p, o);
		if (len == n) {
			for (j = 0; j < n && rd8(p, o + 1 + j) == w[j]; j++)
				;
			if (j == n)
				return true;
		}
		o += 1 + len;
	}
	return false;
}

/* add a lower case word as a candidate unless it is unusable or known */
static bool add(search *s, const uint32_t *w, unsigned n)
{
	osk_keyboard *osk = s->osk;
	osk_candidate *c;
	unsigned i, j;

	if (osk->ncand >= s->limit)
		return false;
	if (n > s->room || n > OSK_CAND_LEN || blocked(&osk->dict, w, n))
		return true;
	c = &osk->cand[osk->ncand];
	for (i = 0; i < n; i++) {
		uint32_t cp = w[i];
		if (s->caps_all || (s->caps_first && i == 0))
			cp = upper(cp);
		if (!osk_accepts(osk, cp))
			return true;
		c->cp[i] = cp;
	}
	c->len = n;
	for (j = 0; j < osk->ncand; j++)
		if (osk->cand[j].len == n && !memcmp(osk->cand[j].cp, c->cp, n * sizeof(uint32_t)))
			return true;
	osk->ncand++;
	return osk->ncand < s->limit;
}

/* depth first walk over the words starting with the typed letters; with
   exact set, only words of the typed length */
static void walk(search *s, walker *w, bool exact)
{
	const osk_predictor *p = w->p;
	u32 node[OSK_CAND_LEN];
	uint32_t word[OSK_CAND_LEN];
	unsigned d = 0, limit = exact ? s->n : OSK_CAND_LEN;

	node[0] = p->graph;
	while (w->steps++ < STEP_BUDGET) {
		u32 o = node[d], child;
		unsigned ci;
		uint32_t c;

		if (o == NONE) {
			if (!d)
				break;
			d--;
			node[d] = next_sibling(w, node[d]);
			continue;
		}
		ci = char_index(w, o);
		if (ci >= p->nchars) {
			node[d] = NONE;
			continue;
		}
		c = be16(p->zsd, p->charset + ci * 2);
		if (d < s->n && (s->strict ? lower(s->typed[d]) != c : !matches(s->typed[d], c))) {
			node[d] = next_sibling(w, o);
			continue;
		}
		word[d] = c;
		if ((flags_of(w, o) & F_TERMINAL) && d + 1 >= s->n && (!exact || d + 1 == s->n)) {
			if (!add(s, word, d + 1))
				return;
		}
		child = first_child(w, o);
		if (child != NONE && d + 1 < limit)
			node[++d] = child;
		else
			node[d] = next_sibling(w, o);
	}
}

/* extra words that start with the typed letters; exact as above */
static void extra(search *s, bool exact)
{
	const osk_predictor *p = &s->osk->dict;
	const u8 *z = p->znd;
	unsigned i, j, n;
	uint32_t word[OSK_CAND_LEN];

	for (i = 0; i < p->nznd; i++) {
		u32 o = be32(z, 4 + i * 4);

		for (n = 0; o + 1 < p->znd_size && n <= OSK_CAND_LEN; n++, o += 2) {
			uint32_t c = be16(z, o);
			if (!c)
				break;
			if (n < OSK_CAND_LEN)
				word[n] = lower(c);
		}
		if (n > OSK_CAND_LEN || n < s->n || (exact && n != s->n))
			continue;
		for (j = 0; j < s->n && matches(s->typed[j], word[j]); j++)
			;
		if (j == s->n && !add(s, word, n))
			return;
	}
}

static void walk_all(search *s, bool exact)
{
	walker w = { &s->osk->dict, 0 };
	walk(s, &w, exact);
}

bool osk_predict_active(const osk_keyboard *osk)
{
	return osk->dict.ok && osk->predict && !osk->numpad &&
	       !(osk->cfg.flags & OSK_FLAG_PASSWORD);
}

bool osk_predict_update(osk_keyboard *osk, bool force)
{
	unsigned start = osk->caret, n, i, letters = 0, upper_count = 0;
	const uint32_t *typed;
	search s;

	while (start > 0 && osk_is_word_char(osk->text[start - 1]))
		start--;
	n = osk->caret - start;
	typed = &osk->text[start];
	for (i = 0; i < n; i++) {
		if (is_letter(typed[i]))
			letters++;
		if (is_upper(typed[i]))
			upper_count++;
	}
	if (!osk_predict_active(osk) || !letters || n > OSK_CAND_LEN ||
	    (osk->caret < osk->len && osk_is_word_char(osk->text[osk->caret])))
		n = 0;

	if (!force && n == osk->word_len && start == osk->word_start &&
	    !memcmp(osk->word, typed, n * sizeof(uint32_t)))
		return false;
	osk->word_start = start;
	osk->word_len = n;
	memcpy(osk->word, typed, n * sizeof(uint32_t));
	osk->ncand = 0;
	osk->cand_first = 0;
	if (!n)
		return true;

	s.osk = osk;
	s.typed = osk->word;
	s.n = n;
	s.caps_first = is_upper(typed[0]);
	s.caps_all = n > 1 && upper_count == letters;
	/* room left after removing the typed word */
	s.room = osk->max_chars - (osk->len - n);

	/* the typed word itself, its accented forms, then the dictionary's
	   order with up to two extra words after the first three */
	s.limit = OSK_MAX_CANDIDATES;
	s.strict = true;
	walk_all(&s, true);
	s.strict = false;
	walk_all(&s, true);
	extra(&s, true);
	s.limit = osk->ncand + 3 < OSK_MAX_CANDIDATES ? osk->ncand + 3 : OSK_MAX_CANDIDATES;
	walk_all(&s, false);
	s.limit = osk->ncand + 2 < OSK_MAX_CANDIDATES ? osk->ncand + 2 : OSK_MAX_CANDIDATES;
	extra(&s, false);
	s.limit = OSK_MAX_CANDIDATES;
	walk_all(&s, false);
	extra(&s, false);
	return true;
}
