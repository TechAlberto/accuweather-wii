/*-------------------------------------------------------------
osk_prompt.c -- blocking, self-contained keyboard prompt
-------------------------------------------------------------*/
#include <malloc.h>
#include <string.h>
#include <wiiuse/wpad.h>

#include "osk_internal.h"

#define FIFO_SIZE	(256 * 1024)

static void *gp_fifo;

static void setup_video(GXRModeObj *rmode)
{
	f32 yscale;
	u32 xfb_height;

	GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
	yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
	xfb_height = GX_SetDispCopyYScale(yscale);
	GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
	GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
	GX_SetDispCopyDst(rmode->fbWidth, xfb_height);
	GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
	GX_SetFieldMode(rmode->field_rendering,
			(rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE);
	GX_SetPixelFmt(rmode->aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetDispCopyGamma(GX_GM_1_0);
	GX_SetCopyClear((GXColor){ 0x20, 0x24, 0x2C, 0xFF }, GX_MAX_Z24);
}

static bool is_cached(void *p)
{
	return ((u32)p & 0xF0000000) == 0x80000000;
}

osk_status OSK_Prompt(const osk_config *cfg, GXRModeObj *rmode, void *xfb,
		      char *out, size_t out_size)
{
	osk_config c;
	osk_keyboard *osk;
	osk_input in;
	osk_status status = OSK_STATUS_CANCELLED;
	u32 fb_size;
	void *saved, *alloc2, *fbs[2];
	int cur = 1;

	if (!rmode || !xfb)
		return OSK_STATUS_CANCELLED;

	if (cfg)
		c = *cfg;
	else
		OSK_DefaultConfig(&c);
	c.screen_width = rmode->fbWidth;
	c.screen_height = rmode->efbHeight;

	fb_size = VIDEO_GetFrameBufferSize(rmode);
	saved = malloc(fb_size);
	alloc2 = memalign(32, fb_size);
	osk = OSK_Create(&c);
	if (!saved || !alloc2 || !osk) {
		free(saved);
		free(alloc2);
		OSK_Destroy(osk);
		return OSK_STATUS_CANCELLED;
	}
	memcpy(saved, xfb, fb_size);
	DCInvalidateRange(alloc2, fb_size);
	fbs[0] = xfb;
	fbs[1] = MEM_K0_TO_K1(alloc2);

	if (!(c.flags & OSK_FLAG_GX_READY) && !gp_fifo) {
		gp_fifo = memalign(32, FIFO_SIZE);
		memset(gp_fifo, 0, FIFO_SIZE);
		GX_Init(gp_fifo, FIFO_SIZE);
	}
	setup_video(rmode);

	WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);
	WPAD_SetVRes(WPAD_CHAN_0, rmode->fbWidth, rmode->efbHeight);

	/* start from a clean EFB */
	GX_CopyDisp(fbs[1], GX_TRUE);
	GX_DrawDone();

	for (;;) {
		WPAD_ScanPads();
		PAD_ScanPads();
		OSK_ReadInput(&in, WPAD_CHAN_0, PAD_CHAN0);
		status = OSK_Update(osk, &in);

		GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
		OSK_Draw(osk);
		GX_DrawDone();
		GX_SetColorUpdate(GX_TRUE);
		GX_CopyDisp(fbs[cur], GX_TRUE);
		GX_Flush();

		VIDEO_SetNextFramebuffer(fbs[cur]);
		VIDEO_Flush();
		VIDEO_WaitVSync();
		cur ^= 1;

		if (status != OSK_STATUS_EDITING)
			break;
	}

	if (status == OSK_STATUS_DONE)
		OSK_CopyText(osk, out, out_size);
	else if (out && out_size)
		out[0] = 0;
	OSK_Destroy(osk);

	/* put the caller's picture back */
	memcpy(xfb, saved, fb_size);
	if (is_cached(xfb))
		DCFlushRange(xfb, fb_size);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	VIDEO_WaitVSync();

	free(saved);
	free(alloc2);
	return status;
}
