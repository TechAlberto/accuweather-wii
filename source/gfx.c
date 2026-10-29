#include "gfx.h"

#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <ogc/conf.h>

#define FIFO_SIZE (256 * 1024)
#define ATLAS_MAX 1024

static GXRModeObj *rmode;
static void *xfb;
static void *fifo;
static Mtx44 proj;

/* Two IA8 textures: the font atlas and the wordmark (two masks).  They swap in
 * and out of GX_TEXMAP0 as the UI draws, so the binding is tracked here rather
 * than reloaded for every quad. */
enum { TEX_NONE = 0, TEX_ATLAS, TEX_SPRITE };

static GXTexObj atlas_obj;
static u8 *atlas_data;
static int atlas_w, atlas_h;

static GXTexObj sprite_obj;
static u8 *sprite_data;
static int sprite_w, sprite_h;

static int tex_bound;

int gfx_w = 640;
int gfx_h = 480;

int gfx_is_widescreen(void)
{
    return CONF_GetAspectRatio() == CONF_ASPECT_16_9;
}

/* Formatted description of the video mode libogc handed us.  Only used by
 * UI_BOOTINFO builds, but it is the quickest way to see why a display looks
 * wrong. */
void gfx_mode_info(char *buf, int n)
{
    snprintf(buf, n, "%dx%d ef%d xfb%d vi%dx%d tv%04x aa%d xfm%d",
             rmode->fbWidth, rmode->efbHeight, rmode->efbHeight,
             rmode->xfbHeight, rmode->viWidth, rmode->viHeight,
             (unsigned)rmode->viTVMode, (int)rmode->aa, (int)rmode->xfbMode);
}

int gfx_init(void)
{
    VIDEO_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    if (!rmode)
        return -1;

    xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    if (!xfb)
        return -1;

    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE)
        VIDEO_WaitVSync();

    fifo = MEM_K0_TO_K1(memalign(32, FIFO_SIZE));
    if (!fifo)
        return -1;
    memset(fifo, 0, FIFO_SIZE);
    GX_Init(fifo, FIFO_SIZE);

    GX_SetCopyClear((GXColor){0, 0, 0, 255}, 0x00ffffff);
    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
    GX_SetDispCopyYScale((f32)rmode->xfbHeight / (f32)rmode->efbHeight);
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopyDst(rmode->fbWidth, rmode->xfbHeight);
    GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
    GX_SetFieldMode(rmode->field_rendering,
                    ((rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));
    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetDispCopyGamma(GX_GM_1_0);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetZMode(GX_FALSE, GX_LEQUAL, GX_FALSE);
    GX_SetZCompLoc(GX_FALSE);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetAlphaUpdate(GX_TRUE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GX_SetNumChans(1);
    GX_SetNumTevStages(1);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

    gfx_w = gfx_is_widescreen() ? 854 : 640;
    gfx_h = 480;

    GX_CopyDisp(xfb, GX_TRUE);
    GX_Flush();
    VIDEO_Flush();
    VIDEO_WaitVSync();
    return 0;
}

void gfx_frame_begin(void)
{
    gfx_w = gfx_is_widescreen() ? 854 : 640;

    guOrtho(proj, 0.0f, (f32)gfx_h, 0.0f, (f32)gfx_w, 0.0f, 1000.0f);
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetNumChans(1);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetZMode(GX_FALSE, GX_LEQUAL, GX_FALSE);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);

    tex_bound = 0;
}

static inline void put_vertex(f32 x, f32 y, u32 c, f32 u, f32 v)
{
    GX_Position3f32(x, y, 0.0f);
    GX_Color4u8((u8)(c >> 24), (u8)(c >> 16), (u8)(c >> 8), (u8)c);
    GX_TexCoord2f32(u, v);
}

static void bind_texture(int which);

void gfx_rect(float x, float y, float w, float h, u32 color)
{
    if (w <= 0.0f || h <= 0.0f)
        return;
    bind_texture(TEX_NONE);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    put_vertex(x, y, color, 0, 0);
    put_vertex(x + w, y, color, 0, 0);
    put_vertex(x + w, y + h, color, 0, 0);
    put_vertex(x, y + h, color, 0, 0);
    GX_End();
}

void gfx_rect_outline(float x, float y, float w, float h, float t, u32 color)
{
    gfx_rect(x, y, w, t, color);
    gfx_rect(x, y + h - t, w, t, color);
    gfx_rect(x, y + t, t, h - 2 * t, color);
    gfx_rect(x + w - t, y + t, t, h - 2 * t, color);
}

void gfx_hline(float x, float y, float w, u32 color)
{
    gfx_rect(x, y, w, 1.0f, color);
}

void gfx_vgrad(float x, float y, float w, float h, u32 top, u32 bottom)
{
    if (w <= 0.0f || h <= 0.0f)
        return;
    bind_texture(TEX_NONE);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    put_vertex(x, y, top, 0, 0);
    put_vertex(x + w, y, top, 0, 0);
    put_vertex(x + w, y + h, bottom, 0, 0);
    put_vertex(x, y + h, bottom, 0, 0);
    GX_End();
}

void gfx_clear(u32 color)
{
    gfx_rect(0, 0, (float)gfx_w, (float)gfx_h, color);
}

void gfx_texture_init(u8 *data, int w, int h)
{
    atlas_data = data;
    atlas_w = w;
    atlas_h = h;
    GX_InitTexObj(&atlas_obj, data, w, h, GX_TF_IA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&atlas_obj, GX_LINEAR, GX_LINEAR);
    tex_bound = TEX_NONE;
    gfx_texture_flush();
}

void gfx_texture_flush(void)
{
    if (!atlas_data)
        return;
    DCStoreRange(atlas_data, (u32)atlas_w * (u32)atlas_h * 2);
    GX_InvalidateTexAll();
    tex_bound = TEX_NONE;
}

/* A second GX_TF_IA8 image, tiled the same way as the atlas: used for the
 * AccuWeather wordmark, drawn once for the white text and once for the orange
 * sun with a tinted vertex colour. */
void gfx_sprite_init(u8 *data, int w, int h)
{
    sprite_data = data;
    sprite_w = w;
    sprite_h = h;
    GX_InitTexObj(&sprite_obj, data, w, h, GX_TF_IA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&sprite_obj, GX_LINEAR, GX_LINEAR);
    tex_bound = TEX_NONE;
    gfx_sprite_flush();
}

void gfx_sprite_flush(void)
{
    if (!sprite_data)
        return;
    DCStoreRange(sprite_data, (u32)sprite_w * (u32)sprite_h * 2);
    GX_InvalidateTexAll();
    tex_bound = TEX_NONE;
}

static void bind_texture(int which)
{
    if (tex_bound == which)
        return;
    if (which == TEX_ATLAS)
        GX_LoadTexObj(&atlas_obj, GX_TEXMAP0);
    else if (which == TEX_SPRITE)
        GX_LoadTexObj(&sprite_obj, GX_TEXMAP0);
    GX_SetTevOp(GX_TEVSTAGE0, which ? GX_MODULATE : GX_PASSCLR);
    tex_bound = which;
}

void gfx_texture_quad(float x, float y, float w, float h,
                      float u0, float v0, float u1, float v1, u32 color)
{
    if (w <= 0.0f || h <= 0.0f)
        return;
    bind_texture(TEX_ATLAS);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    put_vertex(x, y, color, u0, v0);
    put_vertex(x + w, y, color, u1, v0);
    put_vertex(x + w, y + h, color, u1, v1);
    put_vertex(x, y + h, color, u0, v1);
    GX_End();
}

/* Whole sprite, scaled into the given rectangle. */
void gfx_sprite_quad(float x, float y, float w, float h,
                     float u0, float v0, float u1, float v1, u32 color)
{
    if (w <= 0.0f || h <= 0.0f || !sprite_data)
        return;
    bind_texture(TEX_SPRITE);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    put_vertex(x, y, color, u0, v0);
    put_vertex(x + w, y, color, u1, v0);
    put_vertex(x + w, y + h, color, u1, v1);
    put_vertex(x, y + h, color, u0, v1);
    GX_End();
}

int gfx_texture_width(void) { return atlas_w; }
int gfx_texture_height(void) { return atlas_h; }
int gfx_sprite_width(void) { return sprite_w; }
int gfx_sprite_height(void) { return sprite_h; }

void gfx_frame_end(void)
{
    GX_DrawDone();
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);
    GX_CopyDisp(xfb, GX_TRUE);
    GX_Flush();
    VIDEO_Flush();
    VIDEO_WaitVSync();
}
