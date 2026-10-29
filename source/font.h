#ifndef FONT_H
#define FONT_H

#include <gccore.h>

enum {
    FONT_LIGHT = 0,
    FONT_REGULAR,
    FONT_MEDIUM,
    FONT_BOLD,
    FONT_ICONS,
    FONT_COUNT
};

enum { ALIGN_LEFT = 0, ALIGN_CENTER, ALIGN_RIGHT };

int  font_init(void);
void font_frame_begin(void);

/* y is the top of the line box (ascent above the baseline). */
void  font_draw(int font, int px, float x, float y, u32 color, const char *text);
void  font_draw_sp(int font, int px, float x, float y, u32 color, const char *text, float tracking);
void  font_draw_align(int font, int px, float x, float y, float w, int align, u32 color, const char *text);
void  font_draw_center(int font, int px, float cx, float cy, u32 color, const char *text);
void  font_draw_glyph_center(int font, int px, float cx, float cy, u32 color, unsigned codepoint);

float font_width(int font, int px, const char *text);
float font_width_sp(int font, int px, const char *text, float tracking);
int   font_ascent(int font, int px);
int   font_line_height(int font, int px);
float font_cap_height(int font, int px);

/* Draws up to max_lines wrapped lines inside max_w. Returns lines drawn. */
int font_draw_wrapped(int font, int px, float x, float y, float max_w,
                      int max_lines, float line_gap, u32 color, const char *text);

/* Rasterises every glyph the UI can ask for, before the atlas is uploaded. */
void font_prewarm(void);

/* Non-zero if prewarm ran out of atlas room (text would silently vanish). */
int  font_atlas_full(void);

#endif
