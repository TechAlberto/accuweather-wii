#ifndef GFX_H
#define GFX_H

#include <gccore.h>

/* Logical screen size. 640x480 for 4:3, 854x480 for 16:9 (the EFB itself is
 * always 640x480; we widen the logical space on widescreen so the widescreen
 * stretch cancels out and the UI keeps its proportions). */
extern int gfx_w;
extern int gfx_h;

#define RGBA(r, g, b, a) ((u32)(((u32)(r) << 24) | ((u32)(g) << 16) | ((u32)(b) << 8) | (u32)(a)))
#define RGB(r, g, b) RGBA(r, g, b, 255)

int  gfx_init(void);
int  gfx_is_widescreen(void);
void gfx_mode_info(char *buf, int n);
void gfx_frame_begin(void);
void gfx_frame_end(void);

/* Low level 2D primitives. Coordinates are in logical pixels, y grows down. */
void gfx_clear(u32 color);
void gfx_rect(float x, float y, float w, float h, u32 color);
void gfx_rect_outline(float x, float y, float w, float h, float t, u32 color);
void gfx_vgrad(float x, float y, float w, float h, u32 top, u32 bottom);
void gfx_hline(float x, float y, float w, u32 color);

/* Single-channel-alpha texture (GX_TF_IA8) used by the font atlas. */
void gfx_texture_init(u8 *data, int w, int h);
void gfx_texture_quad(float x, float y, float w, float h,
                      float u0, float v0, float u1, float v1, u32 color);
void gfx_texture_flush(void);
int  gfx_texture_width(void);
int  gfx_texture_height(void);

/* A second GX_TF_IA8 image (currently the AccuWeather wordmark, split into a
 * white mask and an orange mask).  Re-uploading either texture releases the
 * GX texture cache, so this is called once at start-up. */
void gfx_sprite_init(u8 *data, int w, int h);
/* Whole sprite, scaled into the given rectangle.  The UVs let a caller pick a
 * half of a stacked image (the wordmark stores its white and orange masks one
 * above the other). */
void gfx_sprite_quad(float x, float y, float w, float h,
                     float u0, float v0, float u1, float v1, u32 color);
void gfx_sprite_flush(void);
int  gfx_sprite_width(void);
int  gfx_sprite_height(void);

#endif
