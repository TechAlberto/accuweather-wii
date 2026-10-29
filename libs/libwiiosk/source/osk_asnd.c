/*-------------------------------------------------------------
osk_asnd.c -- sound effect playback through ASND

Kept in its own object so that only apps calling OSK_InitSound()
need to link -lasnd.
-------------------------------------------------------------*/
#include <stdlib.h>
#include <asndlib.h>

#include "osk_internal.h"

static u8 volume = 255;

static void play(osk_sound sound)
{
	osk_sound_data d;
	s32 voice;
	int pitch;

	if (!OSK_GetSound(sound, &d))
		return;
	voice = ASND_GetFirstUnusedVoice();
	if (voice < 0)
		return;
	pitch = d.rate;
	if (d.jitter_cents) {
		/* 2^(cents / 1200) ~ 1 + cents * ln 2 / 1200 for small values */
		float r = (rand() % 2001 - 1000) / 1000.0f;
		pitch = (int)(d.rate * (1 + r * d.jitter_cents * 0.000577623f));
	}
	DCFlushRange((void *)d.pcm, (d.samples * 2 + 31) & ~31u);
	ASND_SetVoice(voice, VOICE_MONO_16BIT_BE, pitch, 0, (void *)d.pcm,
		      (d.samples * 2 + 31) & ~31u, volume, volume, NULL);
}

bool OSK_InitSound(bool init_asnd)
{
	if (init_asnd) {
		ASND_Init();
		ASND_Pause(0);
	}
	osk_sound_hook = play;
	return true;
}

void OSK_ShutdownSound(void)
{
	osk_sound_hook = NULL;
}

void OSK_SetSoundVolume(u8 v)
{
	volume = v;
}
