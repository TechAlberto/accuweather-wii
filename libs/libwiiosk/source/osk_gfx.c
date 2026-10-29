/*-------------------------------------------------------------
osk_gfx.c -- textures generated at start-up

Every image is drawn from rounded rectangles, polygons and
vertical gradients described below; no bitmaps are stored.
Grey images are later tinted by the material colors of the quad
that uses them (see osk_backend_quad()).
-------------------------------------------------------------*/
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "osk_internal.h"

typedef struct {
	float t, v;
} stop;

#define NSTOPS(a)	(sizeof(a) / sizeof((a)[0]))

static float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

static float mixf(float a, float b, float t)
{
	return a + (b - a) * t;
}

/* piecewise linear through the stops, clamped at both ends */
static float ramp(const stop *s, unsigned n, float t)
{
	unsigned i;

	if (t <= s[0].t)
		return s[0].v;
	for (i = 1; i < n; i++)
		if (t <= s[i].t)
			return mixf(s[i - 1].v, s[i].v, (t - s[i - 1].t) / (s[i].t - s[i - 1].t));
	return s[n - 1].v;
}

/* signed distance to a rounded rectangle, negative inside */
static float sd_rrect(float px, float py, float x0, float y0, float x1, float y1, float r)
{
	float qx = fabsf(px - (x0 + x1) * 0.5f) - ((x1 - x0) * 0.5f - r);
	float qy = fabsf(py - (y0 + y1) * 0.5f) - ((y1 - y0) * 0.5f - r);
	float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
	float in = qx > qy ? qx : qy;

	return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - r;
}

/* pixel coverage from a signed distance */
static float cover(float sd)
{
	return clampf(0.5f - sd, 0.0f, 1.0f);
}

static float rrect(float px, float py, float x0, float y0, float x1, float y1, float r)
{
	return cover(sd_rrect(px, py, x0, y0, x1, y1, r));
}

/* even-odd point in polygon test; xy holds n x,y pairs */
static bool in_poly(const float *xy, int n, float x, float y)
{
	bool in = false;
	int i, j;

	for (i = 0, j = n - 1; i < n; j = i++) {
		float xi = xy[2 * i], yi = xy[2 * i + 1], xj = xy[2 * j], yj = xy[2 * j + 1];
		if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi)
			in = !in;
	}
	return in;
}

/* polygon coverage of pixel (x, y), 4x4 samples */
static float poly(const float *xy, int n, int x, int y)
{
	int i, hits = 0;

	for (i = 0; i < 16; i++)
		hits += in_poly(xy, n, x + ((i & 3) + 0.5f) / 4, y + ((i >> 2) + 0.5f) / 4);
	return hits / 16.0f;
}

static bool alloc_image(osk_image *im, int w, int h)
{
	im->w = w;
	im->h = h;
	im->rgba = calloc(w * h, 4);
	return im->rgba != NULL;
}

static void put(osk_image *im, int x, int y, float r, float g, float b, float a)
{
	u8 *p = &im->rgba[(y * im->w + x) * 4];

	p[0] = (u8)(clampf(r, 0, 255) + 0.5f);
	p[1] = (u8)(clampf(g, 0, 255) + 0.5f);
	p[2] = (u8)(clampf(b, 0, 255) + 0.5f);
	p[3] = (u8)(clampf(a, 0, 1) * 255 + 0.5f);
}

/* intensity image: alpha equals the value, as with GX I8 textures */
static void put_i(osk_image *im, int x, int y, float v)
{
	put(im, x, y, v, v, v, v / 255.0f);
}

/*-------------------------------------------------------------
  flat images
-------------------------------------------------------------*/
static void gen_white(osk_image *im)
{
	int x, y;

	for (y = 0; y < im->h; y++)
		for (x = 0; x < im->w; x++)
			put_i(im, x, y, 255);
}

/* one-texel horizontal stripes; stretched over the screen they turn
   into the soft banding of the background */
static void gen_stripes(osk_image *im)
{
	int x, y;

	for (y = 0; y < im->h; y++)
		for (x = 0; x < im->w; x++)
			put_i(im, x, y, (y & 1) ? 0 : 255);
}

/*-------------------------------------------------------------
  glass keys: dark 1px outline, a bright band across the top and
  a vertical gradient below it
-------------------------------------------------------------*/
static void gen_glass(osk_image *im, float radius, const stop *base, unsigned nbase)
{
	static const stop band[] = {
		{ 1, 179 }, { 2, 227 }, { 3, 206 }, { 4, 192 }, { 5, 184 }, { 6, 181 }, { 7, 162 },
	};
	const float w = im->w, h = im->h;
	int x, y;

	for (y = 0; y < im->h; y++) {
		for (x = 0; x < im->w; x++) {
			float px = x + 0.5f, py = y + 0.5f;
			float a = rrect(px, py, 0, 0, w, h, radius);
			float in = rrect(px, py, 1, 1, w - 1, h - 1, radius - 1);
			float hi = rrect(px, py, 2.5f, 1, w - 2.5f, 8, 3.0f);
			float v = ramp(base, nbase, y);

			v = mixf(v, ramp(band, NSTOPS(band), y), hi);
			put(im, x, y, v * in, v * in, v * in, a);
		}
	}
}

static void gen_glass_key(osk_image *im)
{
	static const stop base[] = {
		{ 3, 133 }, { 7, 143 }, { 8, 144 }, { 20, 178 }, { 32, 221 },
	};
	gen_glass(im, 4.25f, base, NSTOPS(base));
}

static void gen_glass_wide(osk_image *im)
{
	static const stop base[] = {
		{ 3, 133 }, { 7, 143 }, { 8, 148 }, { 20, 182 }, { 31, 221 }, { 32, 217 },
	};
	gen_glass(im, 2.0f, base, NSTOPS(base));
}

/* the "More" plate: light upper half over a darker inset lower half */
static void gen_plate(osk_image *im)
{
	static const stop upper[] = {
		{ 1, 240 }, { 2, 239 }, { 5, 229 }, { 10, 218 }, { 15, 209 }, { 19, 206 },
	};
	static const stop lower[] = {
		{ 20, 173 }, { 30, 200 }, { 38, 226 },
	};
	const float w = im->w, h = im->h;
	int x, y;

	for (y = 0; y < im->h; y++) {
		for (x = 0; x < im->w; x++) {
			float px = x + 0.5f, py = y + 0.5f;
			float a = rrect(px, py, 0, 0, w, h, 11.5f);
			float in = rrect(px, py, 1, 1, w - 1, h - 1, 10.5f);
			float lo = rrect(px, py, 1, 20, w - 1, h + 12, 12.0f);
			float v = mixf(ramp(upper, NSTOPS(upper), y), ramp(lower, NSTOPS(lower), y), lo);

			put(im, x, y, v * in, v * in, v * in, a);
		}
	}
}

/*-------------------------------------------------------------
  Quit / OK pill: blue ring, white top band, pale body with a blue
  glow along the bottom
-------------------------------------------------------------*/
static void gen_pill(osk_image *im)
{
	static const float ring[3] = { 0x20, 0xB8, 0xEC };
	static const float body[3] = { 0xE9, 0xEE, 0xF5 };
	static const float body_low[3] = { 0xE2, 0xE8, 0xF1 };
	static const float side[3] = { 0xF2, 0xF5, 0xF9 };
	static const float glow[3] = { 0xA7, 0xD4, 0xEE };
	static const float band[3] = { 0xFE, 0xFE, 0xFF };
	static const stop glow_y[] = {
		{ 41, 0 }, { 43, 0.25f }, { 46, 0.7f }, { 49, 1.0f }, { 50, 0.85f },
	};
	const float w = im->w, h = im->h, r = h / 2;
	int x, y, c;

	for (y = 0; y < im->h; y++) {
		for (x = 0; x < im->w; x++) {
			float px = x + 0.5f, py = y + 0.5f;
			float a = rrect(px, py, 0.1f, 0.1f, w - 0.1f, h - 0.1f, r - 0.1f);
			float rim = rrect(px, py, 2.5f, 2.5f, w - 2.5f, h - 2.5f, r - 2.5f);
			float in = rrect(px, py, 3.5f, 3.5f, w - 3.5f, h - 3.5f, r - 3.5f);
			float top = in * (1 - rrect(px, py, 3.5f, 17.5f, w - 3.5f, h + 10.5f, r - 3.5f));
			/* horizontal distance from the curved ends */
			float dy = fabsf(py - r), ri = r - 3.5f;
			float edge = dy < ri ? ri - sqrtf(ri * ri - dy * dy) + 3.5f : r;
			float dx = fminf(px, w - px) - edge;
			float sidef = 1 - clampf(dx / 22.0f, 0, 1);
			float glowf = ramp(glow_y, NSTOPS(glow_y), y) * clampf(dx / 18.0f, 0, 1);
			float lowf = clampf((y - 33) / 8.0f, 0, 1);
			float col[3];

			for (c = 0; c < 3; c++) {
				float v = mixf(body[c], body_low[c], lowf);
				v = mixf(v, side[c], sidef * sidef);
				v = mixf(v, glow[c], glowf);
				v = mixf(v, band[c], top);
				v = mixf(255.0f, v, in);	/* light rim inside the ring */
				col[c] = mixf(ring[c], v, rim);
			}
			put(im, x, y, col[0], col[1], col[2], a);
		}
	}
}

/*-------------------------------------------------------------
  toolbar tab / numpad key: dark frame darkening to grey at the
  bottom, white upper panel, inset lower panel
-------------------------------------------------------------*/
static void gen_tool(osk_image *im)
{
	static const stop frame[] = {
		{ 4, 34 }, { 48, 104 }, { 88, 159 },
	};
	static const stop upper[] = {
		{ 6, 253 }, { 7, 255 }, { 10, 255 }, { 12, 249 }, { 46, 222 },
	};
	static const stop lower[] = {
		{ 51, 207 }, { 52, 202 }, { 53, 205 }, { 88, 255 },
	};
	const float w = im->w;
	int x, y;

	for (y = 0; y < im->h; y++) {
		for (x = 0; x < im->w; x++) {
			float px = x + 0.5f, py = y + 0.5f;
			float a = rrect(px, py, 0, 2, w, 95, 9.0f);
			float in = rrect(px, py, 4, 5, w - 4, 91, 5.0f);
			float lo = rrect(px, py, 4, 50.5f, w - 4, 120, 16.0f);
			float lit = rrect(px, py, 4, 47.5f, w - 4, 120, 16.0f) - lo;
			float v = mixf(ramp(upper, NSTOPS(upper), y), ramp(lower, NSTOPS(lower), y), lo);

			v = mixf(v, 228, lit);
			v = mixf(ramp(frame, NSTOPS(frame), y), v, in);
			put(im, x, y, v, v, v, a);
		}
	}
}

/*-------------------------------------------------------------
  icons
-------------------------------------------------------------*/
#define DARK	34.0f

typedef struct {
	u8 x0, x1, y0, y1;	/* inclusive texel ranges */
} cell;

/* dark rounded plate with white cells */
static void gen_cells(osk_image *im, float x0, float y0, float x1, float y1, float r,
		      const cell *cells, unsigned ncells)
{
	int x, y;
	unsigned i;

	for (y = 0; y < im->h; y++) {
		for (x = 0; x < im->w; x++) {
			float a = rrect(x + 0.5f, y + 0.5f, x0, y0, x1, y1, r);
			float v = DARK;

			for (i = 0; i < ncells; i++)
				if (x >= cells[i].x0 && x <= cells[i].x1 && y >= cells[i].y0 && y <= cells[i].y1)
					v = 255;
			put(im, x, y, v, v, v, a);
		}
	}
}

/* keyboard: four rows of keys with a space bar */
static void gen_icon_qwerty(osk_image *im)
{
	static const cell keys[] = {
		{5,9,5,8}, {12,16,5,8}, {19,23,5,8}, {26,30,5,8}, {33,37,5,8}, {40,44,5,8}, {47,51,5,8}, {54,58,5,8},
		{5,12,11,14}, {15,19,11,14}, {22,26,11,14}, {29,33,11,14}, {36,41,11,14}, {44,48,11,14}, {51,58,11,14},
		{5,15,17,20}, {18,22,17,20}, {25,29,17,20}, {32,36,17,20}, {39,43,17,20}, {46,49,17,20}, {52,58,17,20},
		{5,9,23,26}, {12,16,23,26}, {19,37,23,26}, {40,44,23,26}, {47,51,23,26}, {54,58,23,26},
	};
	gen_cells(im, 3, 3, 61, 29, 2.5f, keys, NSTOPS(keys));
}

/* phone keypad: 3 x 4 grid */
static void gen_icon_numpad(osk_image *im)
{
	cell keys[12];
	int i;

	for (i = 0; i < 12; i++) {
		keys[i].x0 = 12 + (i % 3) * 14;
		keys[i].x1 = keys[i].x0 + 11;
		keys[i].y0 = 3 + (i / 3) * 7;
		keys[i].y1 = keys[i].y0 + 4;
	}
	gen_cells(im, 10, 1, 54, 31, 1.5f, keys, 12);
}

/* backspace: left-pointing arrow head on a long shaft */
static void gen_icon_back(osk_image *im)
{
	static const float shape[] = {
		0, 15.5f, 22, 2, 22, 12.5f, 56, 12.5f, 56, 20.5f, 22, 20.5f, 22, 30,
	};
	int x, y;

	for (y = 0; y < im->h; y++)
		for (x = 0; x < im->w; x++)
			put_i(im, x, y, 255 * poly(shape, 7, x, y));
}

/* line feed: bar down the right side turning into a left arrow */
static void gen_icon_enter(osk_image *im)
{
	static const float head[] = {
		3, 32, 25, 18, 25, 46,
	};
	int x, y;

	for (y = 0; y < im->h; y++) {
		for (x = 0; x < im->w; x++) {
			float px = x + 0.5f, py = y + 0.5f;
			/* bar and shaft: the bottom-right corner of a thick
			   rounded outline */
			float outer = rrect(px, py, 0, -40, 53, 36, 12.0f);
			float inner = rrect(px, py, 0, -40, 46, 28, 8.0f);
			float v = py >= 2 && px >= 20 ? clampf(outer - inner, 0, 1) : 0;

			v = fmaxf(v, poly(head, 3, x, y));
			put_i(im, x, y, 255 * v);
		}
	}
}

/* text box scroll arrow: rounded square outline around an up arrow,
   used as a mask */
static void gen_arrow(osk_image *im)
{
	static const float shape[] = {
		24, 10, 39, 41, 24, 32, 9, 41,
	};
	int x, y;

	for (y = 0; y < im->h; y++) {
		for (x = 0; x < im->w; x++) {
			float px = x + 0.5f, py = y + 0.5f;
			float ring = rrect(px, py, 0, 0, 48, 48, 8) - rrect(px, py, 3, 3, 45, 45, 6);
			float v = fmaxf(clampf(ring, 0, 1), poly(shape, 4, x, y));
			put_i(im, x, y, 255 * v);
		}
	}
}

/* prediction on: an open book, two curved pages with ruled lines on a
   dark cover */
static void gen_book_open(osk_image *im)
{
	static const float cover[] = {
		0, 5, 8, 3, 16, 3.5f, 22, 6, 28, 3.5f, 36, 3, 44, 5, 44, 26, 30, 25, 22, 28, 14, 25, 0, 26,
	};
	static const float left[] = {
		2.5f, 2, 9, 0.5f, 16, 1, 21, 4, 21, 24.5f, 15, 22, 8, 21.5f, 2.5f, 23,
	};
	static const float right[] = {
		41.5f, 2, 35, 0.5f, 28, 1, 23, 4, 23, 24.5f, 29, 22, 36, 21.5f, 41.5f, 23,
	};
	int x, y;

	for (y = 0; y < im->h; y++) {
		for (x = 0; x < im->w; x++) {
			float a = poly(cover, 12, x, y);
			float page = fmaxf(poly(left, 8, x, y), poly(right, 8, x, y));
			float v = DARK;

			if (page > 0) {
				/* ruled lines, slightly curved like the pages */
				float c = fabsf(x + 0.5f - 22) / 20.0f;
				float ly = y + 0.5f + c * c * 2.5f;
				bool line = (x >= 5 && x <= 18) || (x >= 26 && x <= 39);
				float r = line && ly > 7 && ly < 21 && fmodf(ly - 7, 4.0f) < 1.0f ? 160 : 255;
				v = mixf(DARK, r, page);
			}
			put(im, x, y, v, v, v, fmaxf(a, page));
		}
	}
}

/* prediction off: a closed book with a label */
static void gen_book_closed(osk_image *im)
{
	int x, y;

	for (y = 0; y < im->h; y++) {
		for (x = 0; x < im->w; x++) {
			float px = x + 0.5f, py = y + 0.5f;
			float a = fmaxf(rrect(px, py, 2, 0, 30, 25, 3), rrect(px, py, 2, 20, 31, 28, 2));
			float cover = rrect(px, py, 4, 2, 28, 22, 2);
			float pages = rrect(px, py, 5, 23, 29, 26, 1) * (1 - rrect(px, py, 2, 0, 30, 24, 3));
			float label = rrect(px, py, 7, 4, 25, 9, 0.5f);
			float v = mixf(DARK, 230, cover);

			v = mixf(v, 170, label);
			v = mixf(v, 255, pages);
			put(im, x, y, v, v, v, a);
		}
	}
}

/* candidate bar arrow: white rounded square, dark rim, dark triangle
   pointing left */
static void gen_scroll(osk_image *im)
{
	static const float tri[] = {
		10, 16, 23, 7.5f, 23, 24.5f,
	};
	int x, y;

	for (y = 0; y < im->h; y++) {
		for (x = 0; x < im->w; x++) {
			float px = x + 0.5f, py = y + 0.5f;
			float a = rrect(px, py, 1, 1, 35, 31, 6);
			float in = rrect(px, py, 2.5f, 2.5f, 33.5f, 29.5f, 4.5f);
			float v = mixf(DARK, 255, in);

			v = mixf(v, DARK, poly(tri, 3, x, y));
			put(im, x, y, v, v, v, a);
		}
	}
}

/* white rounded square, stretched as three slices for highlights */
static void gen_round(osk_image *im)
{
	int x, y;

	for (y = 0; y < im->h; y++)
		for (x = 0; x < im->w; x++)
			put_i(im, x, y, 255 * rrect(x + 0.5f, y + 0.5f, 0, 0, im->w, im->h, 10));
}

/*-------------------------------------------------------------
  text box frame corners: dark ring (tinted blue by the material)
  around white
-------------------------------------------------------------*/
/* distance from the corner center (c, c), counting only the corner side */
static float corner_dist(float px, float py, float c)
{
	return hypotf(fmaxf(c - px, 0), fmaxf(c - py, 0));
}

static void gen_frame(osk_image *im, float dark, float co, float ro, float ci, float ri)
{
	int x, y;

	for (y = 0; y < im->h; y++) {
		for (x = 0; x < im->w; x++) {
			float px = x + 0.5f, py = y + 0.5f;
			float a = cover(corner_dist(px, py, co) - ro);
			float v = mixf(dark, 255, cover(corner_dist(px, py, ci) - ri));

			put(im, x, y, v, v, v, a);
		}
	}
}

static void gen_frame_thick(osk_image *im)
{
	gen_frame(im, DARK, 11.9f, 11.9f, 12.2f, 8.2f);
}

static void gen_frame_thin(osk_image *im)
{
	gen_frame(im, 0, 8.7f, 8.8f, 9.1f, 8.2f);
}

/*-------------------------------------------------------------
  Wii Remote pointer: white arrow with a dark outline and a soft
  shadow
-------------------------------------------------------------*/
static void gen_pointer(osk_image *im)
{
	static const float arrow[] = {
		2, 2, 2, 29, 9, 23, 14, 34, 19, 32, 14, 21, 23, 21,
	};
	static const float outline[] = {
		0, -1, 0, 33.5f, 8.5f, 26.5f, 13, 37, 22, 33, 17.5f, 23.5f, 27.5f, 23.5f,
	};
	int x, y;

	for (y = 0; y < im->h; y++) {
		for (x = 0; x < im->w; x++) {
			float shadow = poly(outline, 7, x - 2, y - 2) * 0.35f;
			float out = poly(outline, 7, x, y);
			float fill = poly(arrow, 7, x, y);
			float a = out + shadow * (1 - out);
			float v = a > 0 ? mixf(0x20, 255, fill) * out / a : 0;

			put(im, x, y, v, v, v, a);
		}
	}
}

/*-------------------------------------------------------------
  table
-------------------------------------------------------------*/
static const struct {
	u16 w, h;
	void (*gen)(osk_image *im);
} image_defs[OSK_TEX_IMAGES] = {
	[OSK_TEX_WHITE]		= {   8,  8, gen_white },
	[OSK_TEX_STRIPES]	= {   8,  8, gen_stripes },
	[OSK_TEX_GLASS_KEY]	= {  42, 34, gen_glass_key },
	[OSK_TEX_GLASS_WIDE]	= {  72, 34, gen_glass_wide },
	[OSK_TEX_GLASS_SPACE]	= { 156, 34, gen_glass_wide },
	[OSK_TEX_PLATE]		= { 174, 40, gen_plate },
	[OSK_TEX_PILL]		= { 180, 54, gen_pill },
	[OSK_TEX_TOOL]		= { 200, 96, gen_tool },
	[OSK_TEX_ICON_QWERTY]	= {  64, 32, gen_icon_qwerty },
	[OSK_TEX_ICON_NUMPAD]	= {  64, 32, gen_icon_numpad },
	[OSK_TEX_ICON_BACK]	= {  56, 32, gen_icon_back },
	[OSK_TEX_ICON_ENTER]	= {  56, 48, gen_icon_enter },
	[OSK_TEX_ARROW]		= {  48, 48, gen_arrow },
	[OSK_TEX_FRAME_THICK]	= {  16, 16, gen_frame_thick },
	[OSK_TEX_FRAME_THIN]	= {   8,  8, gen_frame_thin },
	[OSK_TEX_POINTER]	= {  32, 40, gen_pointer },
	[OSK_TEX_BOOK_OPEN]	= {  44, 28, gen_book_open },
	[OSK_TEX_BOOK_CLOSED]	= {  32, 28, gen_book_closed },
	[OSK_TEX_SCROLL]	= {  36, 32, gen_scroll },
	[OSK_TEX_ROUND]		= {  32, 32, gen_round },
};

bool osk_gfx_generate(osk_image images[OSK_TEX_IMAGES])
{
	int i;

	memset(images, 0, sizeof(osk_image) * OSK_TEX_IMAGES);
	for (i = 0; i < OSK_TEX_IMAGES; i++) {
		if (!alloc_image(&images[i], image_defs[i].w, image_defs[i].h)) {
			osk_gfx_free(images);
			return false;
		}
		image_defs[i].gen(&images[i]);
	}
	return true;
}

void osk_gfx_free(osk_image images[OSK_TEX_IMAGES])
{
	int i;

	for (i = 0; i < OSK_TEX_IMAGES; i++) {
		free(images[i].rgba);
		images[i].rgba = NULL;
	}
}
