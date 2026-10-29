/*-------------------------------------------------------------
osk_sound.c -- decoding of the built-in sound effects

Kept in its own object (with osk_sound_data.c) so that apps without
sound do not link the audio data.
-------------------------------------------------------------*/
#include <stdlib.h>
#ifdef GEKKO
#include <malloc.h>
#endif

#include "osk_internal.h"

static const u16 ima_steps[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60,
	66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337,
	371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552,
	1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
	6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
	22385, 24623, 27086, 29794, 32767,
};

static const s8 ima_index[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

static s16 *decoded[OSK_SOUND_COUNT];
static u8 *adpcm;			/* unpacked on first use, kept */

/* buffers are 32 byte aligned and padded with silence, as ASND needs */
static unsigned osk_sound_bytes(const osk_sound_clip *c)
{
	return (c->samples * 2 + 31) & ~31u;
}

static s16 *decode(const osk_sound_clip *c)
{
	const u8 *in = &adpcm[c->offset];
	unsigned size = osk_sound_bytes(c), i;
	int pred = (s16)(in[0] << 8 | in[1]), index = in[2];
	s16 *out;

#ifdef GEKKO
	out = memalign(32, size);
#else
	out = malloc(size);
#endif
	if (!out)
		return NULL;
	in += 3;
	out[0] = pred;
	for (i = 1; i < c->samples; i++) {
		unsigned code = (in[(i - 1) >> 1] >> (((i - 1) & 1) * 4)) & 15;
		int step = ima_steps[index], diff = step >> 3;

		if (code & 1)
			diff += step >> 2;
		if (code & 2)
			diff += step >> 1;
		if (code & 4)
			diff += step;
		pred += code & 8 ? -diff : diff;
		pred = pred < -32768 ? -32768 : pred > 32767 ? 32767 : pred;
		index += ima_index[code & 7];
		index = index < 0 ? 0 : index > 88 ? 88 : index;
		out[i] = pred;
	}
	for (i = c->samples; i < size / 2; i++)
		out[i] = 0;
	return out;
}

bool OSK_GetSound(osk_sound sound, osk_sound_data *out)
{
	const osk_sound_clip *c;

	if ((unsigned)sound >= OSK_SOUND_COUNT || !out)
		return false;
	c = &osk_sound_clips[sound];
	if (!adpcm)
		adpcm = osk_unpack(osk_sound_packed, osk_sound_packed_size, NULL);
	if (!adpcm)
		return false;
	if (!decoded[sound])
		decoded[sound] = decode(c);
	if (!decoded[sound])
		return false;
	out->pcm = decoded[sound];
	out->samples = c->samples;
	out->rate = c->rate;
	out->jitter_cents = c->jitter_cents;
	return true;
}
