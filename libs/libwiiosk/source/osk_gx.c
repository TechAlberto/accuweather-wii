/*-------------------------------------------------------------
osk_gx.c -- GX drawing backend

Each quad is colored like a layout picture: TEV stage 0 blends
between the material's black (TEVREG0) and white (TEVREG1) by the
texel, stage 1 multiplies by the vertex color.
-------------------------------------------------------------*/
#include <malloc.h>
#include <string.h>

#include "osk_internal.h"

static GXTexObj tex_obj[OSK_TEX_IMAGES + 1];
static void *tex_data[OSK_TEX_IMAGES + 1];
static bool ready;
static int bound;

/* RGBA8 in 4x4 tiles: 32 bytes of AR pairs, then 32 bytes of GB pairs */
static void *tile_rgba8(const osk_image *im)
{
	unsigned w4 = (im->w + 3) & ~3, h4 = (im->h + 3) & ~3, tx, ty, x, y;
	u8 *buf = memalign(32, w4 * h4 * 4), *out = buf;

	if (!buf)
		return NULL;
	for (ty = 0; ty < h4; ty += 4) {
		for (tx = 0; tx < w4; tx += 4) {
			for (y = 0; y < 4; y++) {
				for (x = 0; x < 4; x++) {
					unsigned sx = tx + x, sy = ty + y, i = (y * 4 + x) * 2;
					static const u8 none[4] = { 0, 0, 0, 0 };
					const u8 *p = sx < im->w && sy < im->h ? &im->rgba[(sy * im->w + sx) * 4] : none;

					out[i] = p[3];
					out[i + 1] = p[0];
					out[32 + i] = p[1];
					out[32 + i + 1] = p[2];
				}
			}
			out += 64;
		}
	}
	DCFlushRange(buf, w4 * h4 * 4);
	return buf;
}

/* the glyph atlas is already 4-bit, left texel in the high nibble; I4
   tiles are 8x8 texels, 4 bytes per row */
static void *tile_font(void)
{
	unsigned w = osk_font_atlas_w, h = osk_font_atlas_h, tx, ty, y;
	u8 *atlas = osk_font_unpack(), *buf = memalign(32, w * h / 2), *out = buf;

	if (!atlas || !buf) {
		free(atlas);
		free(buf);
		return NULL;
	}
	for (ty = 0; ty < h; ty += 8) {
		for (tx = 0; tx < w; tx += 8) {
			for (y = 0; y < 8; y++) {
				memcpy(out, &atlas[((ty + y) * w + tx) / 2], 4);
				out += 4;
			}
		}
	}
	free(atlas);
	DCFlushRange(buf, w * h / 2);
	return buf;
}

bool osk_backend_init(void)
{
	osk_image images[OSK_TEX_IMAGES];
	int i;

	if (ready)
		return true;
	if (!osk_gfx_generate(images))
		return false;
	for (i = 0; i < OSK_TEX_IMAGES; i++) {
		tex_data[i] = tile_rgba8(&images[i]);
		if (!tex_data[i])
			break;
		GX_InitTexObj(&tex_obj[i], tex_data[i], images[i].w, images[i].h,
			      GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
		GX_InitTexObjFilterMode(&tex_obj[i], GX_LINEAR, GX_LINEAR);
	}
	osk_gfx_free(images);
	if (i == OSK_TEX_IMAGES) {
		tex_data[OSK_TEX_FONT] = tile_font();
		if (tex_data[OSK_TEX_FONT]) {
			GX_InitTexObj(&tex_obj[OSK_TEX_FONT], tex_data[OSK_TEX_FONT],
				      osk_font_atlas_w, osk_font_atlas_h, GX_TF_I4, GX_CLAMP, GX_CLAMP, GX_FALSE);
			GX_InitTexObjFilterMode(&tex_obj[OSK_TEX_FONT], GX_LINEAR, GX_LINEAR);
			ready = true;
			return true;
		}
	}
	for (i = 0; i <= OSK_TEX_IMAGES; i++) {
		free(tex_data[i]);
		tex_data[i] = NULL;
	}
	return false;
}

void osk_backend_begin(const osk_keyboard *osk)
{
	Mtx44 proj;
	Mtx view;

	guOrtho(proj, 0, osk->vh, 0, osk->vw, 0, 1000.0f);
	GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
	guMtxIdentity(view);
	guMtxTransApply(view, view, 0.0f, 0.0f, -100.0f);
	GX_LoadPosMtxImm(view, GX_PNMTX0);
	GX_SetCurrentMtx(GX_PNMTX0);

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	GX_InvVtxCache();

	GX_SetNumChans(1);
	GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
	GX_SetNumTexGens(1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

	/* stage 0: lerp(black, white, texel) */
	GX_SetNumTevStages(2);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_C0, GX_CC_C1, GX_CC_TEXC, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_A0, GX_CA_A1, GX_CA_TEXA, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	/* stage 1: times the vertex color */
	GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV, GX_CC_RASC, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_APREV, GX_CA_RASA, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);

	GX_InvalidateTexAll();
	bound = -1;

	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetCullMode(GX_CULL_NONE);
	GX_SetColorUpdate(GX_TRUE);
	GX_SetAlphaUpdate(GX_TRUE);
}

static inline void vtx(float x, float y, osk_color c, float s, float t)
{
	GX_Position2f32(x, y);
	GX_Color4u8(c.r, c.g, c.b, c.a);
	GX_TexCoord2f32(s, t);
}

void osk_backend_quad(osk_tex tex, float x0, float y0, float x1, float y1,
		      float s0, float t0, float s1, float t1,
		      osk_color black, osk_color white, const osk_color v[4])
{
	if ((int)tex != bound) {
		GX_LoadTexObj(&tex_obj[tex], GX_TEXMAP0);
		bound = tex;
	}
	GX_SetTevColor(GX_TEVREG0, (GXColor){ black.r, black.g, black.b, black.a });
	GX_SetTevColor(GX_TEVREG1, (GXColor){ white.r, white.g, white.b, white.a });

	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	vtx(x0, y0, v[0], s0, t0);
	vtx(x1, y0, v[1], s1, t0);
	vtx(x1, y1, v[2], s1, t1);
	vtx(x0, y1, v[3], s0, t1);
	GX_End();
}

void osk_backend_end(void)
{
	/* leave a single plain stage behind for the app */
	GX_SetNumTevStages(1);
	GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
}
