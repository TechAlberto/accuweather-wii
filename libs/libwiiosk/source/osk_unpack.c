/*-------------------------------------------------------------
osk_unpack.c -- decoder for the built-in data (font, dictionaries,
sounds), packed by tools/rcpack.py

An adaptive binary range coder with a context model per kind of
data; tools/rcpack.py describes the format and the models.
-------------------------------------------------------------*/
#include <stdlib.h>
#include <string.h>

#include "osk_internal.h"

#define PROB_BITS	12
#define PROB_INIT	2048
#define SHIFT		4
#define TOP		(1u << 24)

enum { MODEL_BYTES = 0, MODEL_NIBBLE, MODEL_IMAGE, MODEL_DICT };

/* MODEL_DICT context bases, 256 probabilities each */
enum { D_OTHER = 0, D_HEAD = 256, D_EXT = 512, D_PTR0 = 528, D_PTRN = 560, D_COUNT = 752 };

static const u8 node_flags[16] = {
	0x10, 0x01, 0x03, 0x14, 0x05, 0x07, 0x1C, 0x0D,
	0x0F, 0x11, 0x13, 0x15, 0x17, 0x1D, 0x1F, 0x35,
};

typedef struct {
	const u8 *in, *end;
	u32 range, code;
} rc;

static u8 next_byte(rc *r)
{
	return r->in < r->end ? *r->in++ : 0;
}

static unsigned bit(rc *r, u16 *p)
{
	u32 bound = (r->range >> PROB_BITS) * *p;
	unsigned b;

	if (r->code < bound) {
		r->range = bound;
		*p += ((1u << PROB_BITS) - *p) >> SHIFT;
		b = 0;
	} else {
		r->code -= bound;
		r->range -= bound;
		*p -= *p >> SHIFT;
		b = 1;
	}
	while (r->range < TOP) {
		r->range <<= 8;
		r->code = r->code << 8 | next_byte(r);
	}
	return b;
}

static unsigned symbol(rc *r, u16 *probs, unsigned nbits)
{
	unsigned node = 1, k;

	for (k = 0; k < nbits; k++)
		node = node * 2 + bit(r, &probs[node]);
	return node - (1u << nbits);
}

/* the role of each byte of a .zsd, see DictContext in tools/rcpack.py */
typedef struct {
	u32 off0, graph, end;
	enum { OTHER, HEAD, EXT, PTR0, PTRN } state;
	u8 prev_head, head;
	u8 queue[2], nqueue;
	u8 width, k;
} dict_ctx;

static unsigned dict_context(const dict_ctx *d, u32 i, const u8 *out)
{
	u8 prev = i ? out[i - 1] : 0;

	if (d->graph && i >= d->graph && i < d->end) {
		switch (d->state) {
		case HEAD:	return D_HEAD + d->prev_head;
		case EXT:	return D_EXT + (d->head >> 4);
		case PTR0:	return D_PTR0 + d->queue[0] * 16 + (d->head >> 4);
		default:	return D_PTRN + (d->width == 2 ? 0 : d->k) * 64 + (prev >> 2);
		}
	}
	return D_OTHER + prev;
}

static void dict_after(dict_ctx *d, u32 i, const u8 *out)
{
	u8 b = out[i];
	bool done = false;

	if (i == 9)
		d->off0 = out[7] << 16 | out[8] << 8 | out[9];
	if (!d->graph && i >= 9 && i == d->off0 + 3) {
		u32 size0 = out[4] << 16 | out[5] << 8 | out[6];
		u32 region = out[d->off0 + 2] << 8 | out[d->off0 + 3];
		d->graph = d->off0 + 4 + region;
		d->end = d->off0 + size0;
		d->state = HEAD;
		return;
	}
	if (!d->graph || i < d->graph || i >= d->end)
		return;

	switch (d->state) {
	case HEAD: {
		u8 f = node_flags[b >> 4];
		d->prev_head = d->head = b;
		d->nqueue = 0;
		if ((f & 1) && (f & 2))
			d->queue[d->nqueue++] = 0;
		if ((f & 4) && (f & 8))
			d->queue[d->nqueue++] = 1;
		if ((b & 15) == 15)
			d->state = EXT;
		else
			done = true;
		break;
	}
	case EXT:
		done = true;
		break;
	case PTR0:
		d->width = b & 0x80 ? 3 : 2;
		d->k = 1;
		d->state = PTRN;
		return;
	default:
		if (++d->k < d->width)
			return;
		d->queue[0] = d->queue[1];
		d->nqueue--;
		done = true;
		break;
	}
	if (done)
		d->state = d->nqueue ? PTR0 : HEAD;
}

u8 *osk_unpack(const u8 *in, size_t in_size, u32 *out_size)
{
	u32 size, i, width;
	unsigned model, nprobs;
	u16 *probs;
	u8 *out;
	rc r;

	if (in_size < 12)
		return NULL;
	size = (u32)in[0] << 24 | in[1] << 16 | in[2] << 8 | in[3];
	model = in[4];
	width = in[5] << 8 | in[6];
	if (size > (16u << 20) || model > MODEL_DICT || (model == MODEL_IMAGE && !width))
		return NULL;

	nprobs = model == MODEL_DICT ? D_COUNT * 256 : model == MODEL_BYTES ? 256 * 256 :
		 model == MODEL_IMAGE ? 4096 * 16 : 256 * 16;
	probs = malloc(nprobs * sizeof(u16));
	out = malloc(size ? size : 1);
	if (!probs || !out) {
		free(probs);
		free(out);
		return NULL;
	}
	for (i = 0; i < nprobs; i++)
		probs[i] = PROB_INIT;

	r.in = in + 7 + 5;
	r.end = in + in_size;
	r.range = 0xFFFFFFFF;
	r.code = (u32)in[8] << 24 | in[9] << 16 | in[10] << 8 | in[11];

	if (model == MODEL_BYTES) {
		for (i = 0; i < size; i++)
			out[i] = symbol(&r, &probs[(i ? out[i - 1] : 0) * 256], 8);
	} else if (model == MODEL_DICT) {
		dict_ctx d;

		memset(&d, 0, sizeof(d));
		for (i = 0; i < size; i++) {
			out[i] = symbol(&r, &probs[dict_context(&d, i, out) * 256], 8);
			dict_after(&d, i, out);
		}
	} else {
		/* nibbles: n(i) reads the i-th decoded one */
#define NIB(j)	(model == MODEL_IMAGE ? ((j) & 1 ? out[(j) / 2] & 15 : out[(j) / 2] >> 4) : \
			 ((j) & 1 ? out[(j) / 2] >> 4 : out[(j) / 2] & 15))
		for (i = 0; i < size * 2; i++) {
			unsigned ctx, v;

			if (model == MODEL_IMAGE) {
				unsigned x = i % width;
				unsigned left = x ? NIB(i - 1) : 0;
				unsigned up = i >= width ? NIB(i - width) : 0;
				unsigned ul = x && i >= width + 1 ? NIB(i - width - 1) : 0;
				unsigned ur = x + 1 < width && i + 1 >= width ? NIB(i - width + 1) : 0;
				ctx = left | up << 4 | (ul >> 2) << 8 | (ur >> 2) << 10;
			} else {
				ctx = (i > 1 ? NIB(i - 2) : 0) << 4 | (i ? NIB(i - 1) : 0);
			}
			v = symbol(&r, &probs[ctx * 16], 4);
			if (model == MODEL_IMAGE)
				out[i / 2] = i & 1 ? (out[i / 2] & 0xF0) | v : v << 4;
			else
				out[i / 2] = i & 1 ? (out[i / 2] & 0x0F) | v << 4 : v;
		}
#undef NIB
	}
	free(probs);
	if (out_size)
		*out_size = size;
	return out;
}
