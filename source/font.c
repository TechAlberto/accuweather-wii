#include "font.h"
#include "gfx.h"

#include <string.h>
#include <stdlib.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "font_light_ttf.h"
#include "font_regular_ttf.h"
#include "font_medium_ttf.h"
#include "font_bold_ttf.h"
#include "font_icons_ttf.h"

#define ATLAS_W 1024
#define ATLAS_H 1024
#define GLYPH_SLOTS 8192
#define MAX_SIZES 40

typedef struct {
    u32 key;       /* (font << 24) | (px << 16) | codepoint */
    u16 ax, ay;    /* atlas position */
    u16 w, h;      /* bitmap size */
    s16 bx, by;    /* bearing */
    u16 adv;       /* advance */
} glyph_t;

typedef struct {
    int px;
    int ascent;
    int descent;
    int lineh;
} sizeinfo_t;

static FT_Library ft_lib;
static FT_Face faces[FONT_COUNT];

static const u8 *face_data[FONT_COUNT];
static u32 face_size[FONT_COUNT];

static u8 atlas[ATLAS_W * ATLAS_H * 2] ATTRIBUTE_ALIGN(32);
static glyph_t glyphs[GLYPH_SLOTS];
static sizeinfo_t sinfo[FONT_COUNT][MAX_SIZES];
static int sinfo_count[FONT_COUNT];

static int alloc_x = 4, alloc_y = 4, shelf_h = 0;
static volatile int atlas_dirty;
static int atlas_full;

/* GX_TF_IA8 uses 32-byte 4x4 tiles (16-bit texels). */
static inline u8 *atlas_ptr(int x, int y)
{
    return atlas + ((((y >> 2) * (ATLAS_W >> 2)) + (x >> 2)) << 5) +
                  ((y & 3) << 3) + ((x & 3) << 1);
}

static void atlas_store(int x, int y, int w, int h, const u8 *src, int pitch)
{
    for (int row = 0; row < h; row++) {
        const u8 *s = src + row * pitch;
        for (int col = 0; col < w; col++) {
            u8 *p = atlas_ptr(x + col, y + row);
            /* GX_TF_IA8 stores alpha in the low byte and intensity in the high
               byte.  Coverage must go to alpha, and intensity stays white --
               the other way round draws dark text that vanishes on the dark
               backgrounds this UI uses. */
            p[0] = s[col];
            p[1] = 255;
        }
    }
    atlas_dirty = 1;
}

static int atlas_alloc(int w, int h, int *ox, int *oy)
{
    if (w <= 0 || h <= 0)
        return 0;
    if (w > ATLAS_W - 8 || h > ATLAS_H - 8)
        return 0;
    if (alloc_x + w + 2 > ATLAS_W) {
        alloc_x = 4;
        alloc_y += shelf_h + 2;
        shelf_h = 0;
    }
    if (alloc_y + h + 2 > ATLAS_H) {
        atlas_full = 1;
        return 0;
    }
    *ox = alloc_x;
    *oy = alloc_y;
    alloc_x += w + 2;
    if (h > shelf_h)
        shelf_h = h;
    return 1;
}

static inline u32 glyph_key(int font, int px, u32 cp)
{
    return ((u32)font << 24) | ((u32)(px & 0xff) << 16) | (cp & 0xffff);
}

static glyph_t *glyph_find(u32 key)
{
    u32 i = (key * 2654435761u) & (GLYPH_SLOTS - 1);
    for (int n = 0; n < 64; n++) {
        glyph_t *g = &glyphs[i];
        if (g->key == key)
            return g;
        if (g->key == 0)
            return NULL;
        i = (i + 1) & (GLYPH_SLOTS - 1);
    }
    return NULL;
}

static glyph_t *glyph_insert(u32 key)
{
    u32 i = (key * 2654435761u) & (GLYPH_SLOTS - 1);
    for (int n = 0; n < 64; n++) {
        glyph_t *g = &glyphs[i];
        if (g->key == 0 || g->key == key) {
            memset(g, 0, sizeof(*g));
            g->key = key;
            return g;
        }
        i = (i + 1) & (GLYPH_SLOTS - 1);
    }
    return NULL;
}

static sizeinfo_t *size_info(int font, int px)
{
    for (int i = 0; i < sinfo_count[font]; i++)
        if (sinfo[font][i].px == px)
            return &sinfo[font][i];

    if (sinfo_count[font] >= MAX_SIZES)
        return &sinfo[font][0];

    FT_Face face = faces[font];
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)px);
    sizeinfo_t *s = &sinfo[font][sinfo_count[font]++];
    s->px = px;
    s->ascent = (int)((face->size->metrics.ascender + 63) >> 6);
    s->descent = (int)((-face->size->metrics.descender + 63) >> 6);
    s->lineh = (int)((face->size->metrics.height + 63) >> 6);
    if (s->lineh < px)
        s->lineh = px;
    return s;
}

static glyph_t *glyph_get(int font, int px, u32 cp)
{
    u32 key = glyph_key(font, px, cp);
    glyph_t *g = glyph_find(key);
    if (g)
        return g;

    g = glyph_insert(key);
    if (!g)
        return NULL;

    size_info(font, px);

    /* size_info() only registers metrics the first time it sees a size, so the
     * face is left at whatever size was set last.  Always set it before
     * rasterising or glyphs come out at the wrong size (and a wrong-size
     * bitmap arriving under a small key fills the atlas and hides text). */
    FT_Set_Pixel_Sizes(faces[font], 0, (FT_UInt)px);

    FT_Error fterr = FT_Load_Char(faces[font], cp, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL);
    if (fterr != 0)
        return g;

    FT_GlyphSlot slot = faces[font]->glyph;
    int w = (int)slot->bitmap.width;
    int h = (int)slot->bitmap.rows;
    g->adv = (u16)((slot->advance.x + 32) >> 6);
    g->bx = (s16)slot->bitmap_left;
    g->by = (s16)slot->bitmap_top;

    if (w > 0 && h > 0 && !atlas_full) {
        int ax, ay;
        if (atlas_alloc(w, h, &ax, &ay)) {
            atlas_store(ax, ay, w, h, slot->bitmap.buffer, slot->bitmap.pitch);
            g->ax = (u16)ax;
            g->ay = (u16)ay;
            g->w = (u16)w;
            g->h = (u16)h;
        }
    }
    return g;
}

static u32 utf8_next(const char **sp)
{
    const unsigned char *s = (const unsigned char *)*sp;
    u32 c = *s++;
    if (c >= 0x80) {
        if ((c & 0xe0) == 0xc0 && (s[0] & 0xc0) == 0x80) {
            c = ((c & 0x1f) << 6) | (s[0] & 0x3f);
            s += 1;
        } else if ((c & 0xf0) == 0xe0 && (s[0] & 0xc0) == 0x80 && (s[1] & 0xc0) == 0x80) {
            c = ((c & 0x0f) << 12) | ((s[0] & 0x3f) << 6) | (s[1] & 0x3f);
            s += 2;
        } else if ((c & 0xf8) == 0xf0 && (s[0] & 0xc0) == 0x80 && (s[1] & 0xc0) == 0x80 &&
                   (s[2] & 0xc0) == 0x80) {
            c = ((c & 0x07) << 18) | ((s[0] & 0x3f) << 12) | ((s[1] & 0x3f) << 6) | (s[2] & 0x3f);
            s += 3;
        } else {
            c = '?';
        }
    }
    *sp = (const char *)s;
    return c;
}

int font_init(void)
{
    face_data[FONT_LIGHT] = font_light_ttf;
    face_size[FONT_LIGHT] = font_light_ttf_size;
    face_data[FONT_REGULAR] = font_regular_ttf;
    face_size[FONT_REGULAR] = font_regular_ttf_size;
    face_data[FONT_MEDIUM] = font_medium_ttf;
    face_size[FONT_MEDIUM] = font_medium_ttf_size;
    face_data[FONT_BOLD] = font_bold_ttf;
    face_size[FONT_BOLD] = font_bold_ttf_size;
    face_data[FONT_ICONS] = font_icons_ttf;
    face_size[FONT_ICONS] = font_icons_ttf_size;

    if (FT_Init_FreeType(&ft_lib) != 0)
        return -1;

    for (int i = 0; i < FONT_COUNT; i++) {
        if (FT_New_Memory_Face(ft_lib, face_data[i], (FT_Long)face_size[i], 0, &faces[i]) != 0) {
            faces[i] = NULL;
            return -1;
        }
    }

    memset(atlas, 0, sizeof(atlas));
    memset(glyphs, 0, sizeof(glyphs));
    memset(sinfo, 0, sizeof(sinfo));
    memset(sinfo_count, 0, sizeof(sinfo_count));
    gfx_texture_init(atlas, ATLAS_W, ATLAS_H);
    atlas_dirty = 0;
    return 0;
}

/* Every glyph the UI can ask for is rasterised before the first frame, so the
 * atlas is uploaded once, complete.  Dolphin's texture cache decides whether a
 * texture changed by hashing samples of it; a glyph added later only writes a
 * few hundred bytes and can be missed, leaving that string invisible until the
 * atlas changes again.  Prewarming keeps all text on the initial upload. */
static void prewarm_charset(int font, int px, const char *chars)
{
    const char *p = chars;
    while (*p) {
        u32 cp = utf8_next(&p);
        glyph_get(font, px, cp);
    }
}

void font_prewarm(void)
{
    static const char ASCII[] =
        " !\"#$%&'()*+,-./0123456789:;<=>?@"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
        "abcdefghijklmnopqrstuvwxyz{|}~"
        "\xc2\xb0"                      /* degree sign */
        "\xc2\xb7"                      /* middle dot */
        "\xe2\x80\xa6"                  /* ellipsis */
        "\xe2\x88\x92"                  /* minus sign */
        "\xe2\x96\xb2\xe2\x96\xbc"      /* up / down triangles */
        "\xe2\x97\x80\xe2\x96\xb6";     /* left / right triangles */

    /* Exactly the sizes ui.c asks for.  Rasterising sizes the UI never uses
     * only wastes atlas space -- and with 92 px icons in the mix the atlas is
     * the scarce resource, not the FreeType call. */
    static const struct { u8 font, px; } text_sizes[] = {
        { FONT_REGULAR,  9 }, { FONT_REGULAR, 10 }, { FONT_REGULAR, 11 },
        { FONT_REGULAR, 12 }, { FONT_REGULAR, 13 },
        { FONT_MEDIUM,  13 }, { FONT_MEDIUM,  14 }, { FONT_MEDIUM,  15 },
        { FONT_MEDIUM,  20 }, { FONT_MEDIUM,  23 },
        { FONT_BOLD,     9 }, { FONT_BOLD,    10 }, { FONT_BOLD,    11 },
        { FONT_BOLD,    12 }, { FONT_BOLD,    13 }, { FONT_BOLD,    15 },
        { FONT_BOLD,    26 },
    };

    for (unsigned i = 0; i < sizeof(text_sizes) / sizeof(text_sizes[0]); i++)
        prewarm_charset(text_sizes[i].font, text_sizes[i].px, ASCII);

    /* The hero temperature is the only very large text. */
    prewarm_charset(FONT_LIGHT, 92, "0123456789-+./\xc2\xb0");

    /* Weather icons are drawn at several scales (hero, hourly strip, hourly
     * rows, daily rows).  Cover every size those produce so the icon glyphs
     * are never added after the first upload. */
    static const int icon_bases[] = { 112, 26, 20, 18 };
    static const float icon_scale[] = {
        0.18f, 0.40f, 0.62f, 0.74f, 0.82f, 0.86f, 0.92f, 0.98f
    };
    static const u32 icon_cp[] = {
        0xf185, 0xf186, 0xf0c2, 0xf0e7, 0xf043, 0xf069, 0xf0e9, 0xf071,
    };

    for (unsigned b = 0; b < sizeof(icon_bases) / sizeof(icon_bases[0]); b++) {
        for (unsigned s = 0; s < sizeof(icon_scale) / sizeof(icon_scale[0]); s++) {
            int px = (int)((float)icon_bases[b] * icon_scale[s]);
            if (px < 1)
                continue;
            for (unsigned c = 0; c < sizeof(icon_cp) / sizeof(icon_cp[0]); c++)
                glyph_get(FONT_ICONS, px, icon_cp[c]);
        }
    }

    /* Alerts tab glyphs are drawn on their own. */
    glyph_get(FONT_ICONS, 16, 0xf071);
    glyph_get(FONT_ICONS, 42, 0xf0e9);
}

int font_atlas_full(void)
{
    return atlas_full;
}

void font_frame_begin(void)
{
    if (atlas_dirty) {
        gfx_texture_flush();
        atlas_dirty = 0;
    }
}


int font_ascent(int font, int px) { return size_info(font, px)->ascent; }
int font_line_height(int font, int px) { return size_info(font, px)->lineh; }

float font_cap_height(int font, int px)
{
    (void)font;
    /* Roboto cap height is ~0.71 em. */
    return (float)px * 0.71f;
}

float font_width_sp(int font, int px, const char *text, float tracking)
{
    float w = 0.0f;
    const char *p = text;
    while (*p) {
        u32 cp = utf8_next(&p);
        if (cp == '\n')
            break;
        glyph_t *g = glyph_get(font, px, cp);
        if (g)
            w += (float)g->adv + tracking;
    }
    return w;
}

float font_width(int font, int px, const char *text)
{
    return font_width_sp(font, px, text, 0.0f);
}

void font_draw_sp(int font, int px, float x, float y, u32 color, const char *text, float tracking)
{
    sizeinfo_t *si = size_info(font, px);
    float baseline = y + (float)si->ascent;
    float pen = x;
    const char *p = text;

    while (*p) {
        u32 cp = utf8_next(&p);
        if (cp == '\n')
            break;
        glyph_t *g = glyph_get(font, px, cp);
        if (!g) {
            pen += (float)px * 0.5f + tracking;
            continue;
        }
        if (g->w && g->h) {
            float u0 = (float)g->ax / (float)gfx_texture_width();
            float v0 = (float)g->ay / (float)gfx_texture_height();
            float u1 = (float)(g->ax + g->w) / (float)gfx_texture_width();
            float v1 = (float)(g->ay + g->h) / (float)gfx_texture_height();
            gfx_texture_quad(pen + g->bx, baseline - g->by,
                             (float)g->w, (float)g->h, u0, v0, u1, v1, color);
        }
        pen += (float)g->adv + tracking;
    }
}

void font_draw(int font, int px, float x, float y, u32 color, const char *text)
{
    font_draw_sp(font, px, x, y, color, text, 0.0f);
}

void font_draw_align(int font, int px, float x, float y, float w, int align, u32 color, const char *text)
{
    float tw = font_width(font, px, text);
    float tx = x;
    if (align == ALIGN_CENTER)
        tx = x + (w - tw) * 0.5f;
    else if (align == ALIGN_RIGHT)
        tx = x + w - tw;
    font_draw(font, px, tx, y, color, text);
}

void font_draw_center(int font, int px, float cx, float cy, u32 color, const char *text)
{
    float tw = font_width(font, px, text);
    int lh = font_line_height(font, px);
    font_draw(font, px, cx - tw * 0.5f, cy - (float)lh * 0.5f, color, text);
}

void font_draw_glyph_center(int font, int px, float cx, float cy, u32 color, unsigned codepoint)
{
    glyph_t *g = glyph_get(font, px, codepoint);
    if (!g || !g->w || !g->h)
        return;
    float u0 = (float)g->ax / (float)gfx_texture_width();
    float v0 = (float)g->ay / (float)gfx_texture_height();
    float u1 = (float)(g->ax + g->w) / (float)gfx_texture_width();
    float v1 = (float)(g->ay + g->h) / (float)gfx_texture_height();
    gfx_texture_quad(cx - (float)g->w * 0.5f, cy - (float)g->h * 0.5f,
                     (float)g->w, (float)g->h, u0, v0, u1, v1, color);
}

int font_draw_wrapped(int font, int px, float x, float y, float max_w,
                      int max_lines, float line_gap, u32 color, const char *text)
{
    int lh = font_line_height(font, px);
    float line_y = y;
    int lines = 0;
    const char *p = text;

    while (*p && lines < max_lines) {
        /* skip leading spaces / newlines */
        while (*p == ' ' || *p == '\n' || *p == '\r')
            p++;
        if (!*p)
            break;

        /* measure candidate words */
        const char *line_start = p;
        const char *last_fit = p;
        const char *q = p;
        float w = 0.0f;

        while (*q && *q != '\n') {
            const char *word_start = q;
            while (*q && *q != ' ' && *q != '\n')
                q++;
            /* width of the word including preceding space */
            float seg = 0.0f;
            for (const char *r = word_start; r < q; r++) {
                const char *rp = r;
                u32 cp = utf8_next(&rp);
                glyph_t *g = glyph_get(font, px, cp);
                if (g)
                    seg += (float)g->adv;
                r = rp - 1;
            }
            float space = (q > line_start) ? font_width(font, px, " ") : 0.0f;
            if (w + space + seg > max_w && q > line_start)
                break;
            w += space + seg;
            last_fit = q;
            while (*q == ' ')
                q++;
            if (*q == '\n')
                break;
        }

        if (last_fit == line_start && *q && *q != '\n') {
            /* a single word longer than the line: hard break at max width */
            const char *r = line_start;
            float seg = 0.0f;
            while (*r && *r != ' ' && *r != '\n') {
                const char *rp = r;
                u32 cp = utf8_next(&rp);
                glyph_t *g = glyph_get(font, px, cp);
                if (g && seg + (float)g->adv > max_w)
                    break;
                if (g)
                    seg += (float)g->adv;
                r = rp;
            }
            last_fit = r;
        }

        char buf[512];
        int n = (int)(last_fit - line_start);
        if (n > (int)sizeof(buf) - 1)
            n = (int)sizeof(buf) - 1;
        memcpy(buf, line_start, (size_t)n);
        buf[n] = 0;

        /* trim trailing spaces */
        while (n > 0 && (buf[n - 1] == ' ' || buf[n - 1] == '\r'))
            buf[--n] = 0;

        font_draw(font, px, x, line_y, color, buf);
        line_y += (float)lh + line_gap;
        lines++;
        p = last_fit;
        while (*p == ' ')
            p++;
    }
    return lines;
}
