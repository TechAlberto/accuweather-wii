/*-------------------------------------------------------------
wiiosk.h -- on-screen software keyboard for Wii homebrew (libogc)

This software is provided 'as-is', without any express or implied
warranty. See LICENSE for the full zlib license text.
-------------------------------------------------------------*/
#ifndef __WIIOSK_H__
#define __WIIOSK_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <gccore.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIIOSK_VERSION_MAJOR	2
#define WIIOSK_VERSION_MINOR	2
#define WIIOSK_VERSION_PATCH	0
#define WIIOSK_VERSION_STRING	"2.2.0"

/* Hard upper bound for the number of characters (code points) in the buffer. */
#define OSK_MAX_CHARS		2048

typedef struct osk_keyboard osk_keyboard;

/* Key arrangement. Every letter layout also has a "More" page with accented
   Latin-1 letters and extra symbols. The toolbar tabs switch between the
   letter layout and the numpad unless OSK_FLAG_LOCK_LAYOUT is set. */
typedef enum {
	OSK_LAYOUT_QWERTY = 0,	/* US English */
	OSK_LAYOUT_QWERTZ,	/* German */
	OSK_LAYOUT_AZERTY,	/* French */
	OSK_LAYOUT_NUMPAD,	/* digits, '.', ',', '-' */
	OSK_LAYOUT_QWERTY_UK,	/* since 2.1: UK English */
	OSK_LAYOUT_QWERTY_IT,	/* Italian */
	OSK_LAYOUT_QWERTY_ES,	/* Spanish */
	OSK_LAYOUT_QWERTY_NL	/* Dutch */
} osk_layout;

enum {
	OSK_FLAG_PASSWORD	= 1 << 0,	/* show '*' instead of the text */
	OSK_FLAG_WIDESCREEN	= 1 << 1,	/* 16:9 output, compensate horizontal stretch */
	OSK_FLAG_NO_CANCEL	= 1 << 2,	/* hide Quit, ignore cancel buttons */
	OSK_FLAG_REQUIRE_TEXT	= 1 << 3,	/* OK is rejected while the text is empty */
	OSK_FLAG_NO_HINTS	= 1 << 4,	/* no effect since 2.0 (there is no help line) */
	OSK_FLAG_NO_POINTER	= 1 << 5,	/* do not draw the Wii Remote pointer */
	OSK_FLAG_GX_READY	= 1 << 6,	/* OSK_Prompt(): GX was already initialised by the app */
	OSK_FLAG_MULTILINE	= 1 << 7,	/* Enter inserts a line break instead of confirming */
	OSK_FLAG_NO_ENTER	= 1 << 8,	/* hide the Enter key */
	OSK_FLAG_LOCK_LAYOUT	= 1 << 9,	/* hide the letters / numpad tabs */
	OSK_FLAG_PREDICT_OFF	= 1 << 10	/* start with word prediction switched off */
};

/* Word list for predictive text: an eZiText static dictionary (.zsd) and
   optionally an extra word list (.znd), both kept in memory by the app.
   The built-in dictionaries below are separate objects, so only the ones
   an app references are linked (each adds 55-80 KB). They are stored
   compressed (packed != NULL, zsd and znd NULL) and unpacked the first
   time a keyboard uses them (80-115 KB each, kept until
   OSK_FreeDictionaries()). */
typedef struct {
	const char *name;		/* UTF-8, shown on the prediction key */
	const void *zsd;
	size_t zsd_size;
	const void *znd;		/* may be NULL */
	size_t znd_size;
	const void *packed;		/* built-in only: zsd + znd, compressed */
	size_t packed_size;
} osk_dictionary;

extern const osk_dictionary osk_dict_en_us;	/* English (US) */
extern const osk_dictionary osk_dict_en_gb;	/* English (UK) */
extern const osk_dictionary osk_dict_de;	/* German */
extern const osk_dictionary osk_dict_fr;	/* French */
extern const osk_dictionary osk_dict_fr_ca;	/* French (Canada) */
extern const osk_dictionary osk_dict_es;	/* Spanish */
extern const osk_dictionary osk_dict_es_la;	/* Spanish (Latin America) */
extern const osk_dictionary osk_dict_it;	/* Italian */
extern const osk_dictionary osk_dict_nl;	/* Dutch */

/* Dictionary languages. */
typedef enum {
	OSK_LANG_NONE = 0,	/* no dictionary (e.g. Japanese, Chinese, Korean) */
	OSK_LANG_EN_US,
	OSK_LANG_EN_GB,
	OSK_LANG_DE,
	OSK_LANG_FR,
	OSK_LANG_FR_CA,
	OSK_LANG_ES,
	OSK_LANG_ES_LA,
	OSK_LANG_IT,
	OSK_LANG_NL
} osk_language;

typedef enum {
	OSK_STATUS_EDITING = 0,
	OSK_STATUS_DONE,	/* OK pressed, text accepted */
	OSK_STATUS_CANCELLED
} osk_status;

/* Delivered through osk_config.on_event, e.g. to play sounds or rumble. */
typedef enum {
	OSK_EVENT_FOCUS = 0,	/* the highlighted key changed */
	OSK_EVENT_PRESS,	/* a key was activated */
	OSK_EVENT_REJECT,	/* input refused (full buffer, filter, empty OK) */
	OSK_EVENT_DONE,
	OSK_EVENT_CANCEL
} osk_event;

/* Sound effects of the Wii keyboard, delivered through osk_config.on_sound
   or played by OSK_InitSound(). */
typedef enum {
	OSK_SOUND_FOCUS = 0,	/* the highlighted key changed */
	OSK_SOUND_INPUT,	/* a character was typed */
	OSK_SOUND_DELETE,	/* Backspace / Delete */
	OSK_SOUND_ERROR,	/* input refused */
	OSK_SOUND_CARET,	/* caret moved by a button */
	OSK_SOUND_CARET_FIX,	/* caret placed with the pointer */
	OSK_SOUND_CANDIDATE,	/* a suggested word was chosen */
	OSK_SOUND_SCROLL,	/* candidate bar or text box scrolled */
	OSK_SOUND_SHIFT_ON,	/* Shift or Caps latched */
	OSK_SOUND_SHIFT_OFF,
	OSK_SOUND_MORE,		/* More page opened */
	OSK_SOUND_BACK,		/* back from the More page */
	OSK_SOUND_PREDICT_ON,
	OSK_SOUND_PREDICT_OFF,
	OSK_SOUND_LETTERS,	/* letters tab */
	OSK_SOUND_NUMPAD,	/* numpad tab */
	OSK_SOUND_OPEN,		/* keyboard created */
	OSK_SOUND_OK,		/* text accepted */
	OSK_SOUND_CANCEL,
	OSK_SOUND_COUNT
} osk_sound;

/* RGBA8 colors, 0xRRGGBBAA. The keyboard is drawn with the look of the
   Wii system keyboard; only these fields still have an effect:
     panel		background
     focus_top		tint of the highlighted key
     active		tint of a latched Shift / Caps key
     key_text		character key labels
     key_text_disabled	labels of keys the filter refuses
     textbox_border	text box frame
     text, caret	typed text and cursor
     title		placeholder shown while the text is empty
   The others are kept for source compatibility and ignored. */
typedef struct {
	u32 overlay;		/* full-screen dim drawn behind the keyboard */
	u32 panel;
	u32 key_top, key_bottom;
	u32 special_top, special_bottom;
	u32 focus_top, focus_bottom;
	u32 active;		/* latched Shift / Caps / Alt */
	u32 key_text;
	u32 key_text_disabled;
	u32 textbox;
	u32 textbox_border;
	u32 text;
	u32 caret;
	u32 title;
	u32 hint;
} osk_theme;

typedef struct {
	const char *title;		/* UTF-8 placeholder shown while the text box is empty, may be NULL */
	const char *initial;		/* UTF-8 initial text, may be NULL */
	unsigned max_chars;		/* 1..OSK_MAX_CHARS, 0 means OSK_MAX_CHARS */
	osk_layout layout;
	unsigned flags;			/* OSK_FLAG_* */
	unsigned screen_width;		/* render target, usually rmode->fbWidth (0 = 640) */
	unsigned screen_height;		/* usually rmode->efbHeight (0 = 480) */
	const osk_theme *theme;		/* NULL selects the built-in theme */

	/* Optional: return false to refuse a code point. Keys whose character is
	   refused are drawn disabled. */
	bool (*filter)(uint32_t codepoint, void *user);
	void (*on_event)(osk_keyboard *osk, osk_event event, void *user);
	void *user;

	/* Since 2.1. Word prediction: shows a prediction key on the letter
	   pages and, while it is on, a bar of suggested words above the keys.
	   NULL disables prediction. Never used with OSK_FLAG_PASSWORD. */
	const osk_dictionary *dictionary;

	/* Since 2.1. Called for every sound effect. When NULL, the sound is
	   played through ASND if OSK_InitSound() succeeded. */
	void (*on_sound)(osk_keyboard *osk, osk_sound sound, void *user);
} osk_config;

/* Abstract buttons understood by OSK_Update(). */
enum {
	OSK_BTN_SELECT		= 1 << 0,	/* press focused key */
	OSK_BTN_BACKSPACE	= 1 << 1,
	OSK_BTN_DELETE		= 1 << 2,
	OSK_BTN_UP		= 1 << 3,	/* move key focus */
	OSK_BTN_DOWN		= 1 << 4,
	OSK_BTN_LEFT		= 1 << 5,
	OSK_BTN_RIGHT		= 1 << 6,
	OSK_BTN_CARET_LEFT	= 1 << 7,	/* move text cursor */
	OSK_BTN_CARET_RIGHT	= 1 << 8,
	OSK_BTN_CARET_HOME	= 1 << 9,
	OSK_BTN_CARET_END	= 1 << 10,
	OSK_BTN_SHIFT		= 1 << 11,
	OSK_BTN_ALT		= 1 << 12,	/* toggle the "More" page */
	OSK_BTN_SPACE		= 1 << 13,
	OSK_BTN_CONFIRM		= 1 << 14,
	OSK_BTN_CANCEL		= 1 << 15
};

#define OSK_INPUT_MAX_TEXT	32

/* One frame of input. Fill it with OSK_ReadInput() or build it yourself. */
typedef struct {
	u32 down;			/* OSK_BTN_* newly pressed this frame */
	u32 held;			/* OSK_BTN_* currently held (enables auto-repeat) */
	bool pointer_valid;
	float pointer_x, pointer_y;	/* in screen_width x screen_height pixels */
	uint32_t text[OSK_INPUT_MAX_TEXT];	/* typed code points (USB keyboard) */
	unsigned text_count;
} osk_input;

/* Fill cfg with defaults: QWERTY, 640x480, 64 chars, built-in theme. */
void OSK_DefaultConfig(osk_config *cfg);
const osk_theme *OSK_DefaultTheme(void);

osk_keyboard *OSK_Create(const osk_config *cfg);
void OSK_Destroy(osk_keyboard *osk);

/* Gather input after WPAD_ScanPads() / PAD_ScanPads(). Pass -1 to skip a
   controller type. The Wii Remote channel needs WPAD_FMT_BTNS_ACC_IR and
   WPAD_SetVRes(chan, screen_width, screen_height) for pointing. USB keyboard
   events are included once OSK_InitUSBKeyboard() succeeded. */
void OSK_ReadInput(osk_input *in, int wpad_chan, int pad_chan);

/* Optional USB keyboard support through libwiikeyboard (link -lwiikeyboard). */
bool OSK_InitUSBKeyboard(void);
void OSK_ShutdownUSBKeyboard(void);

/* Advance one frame. Call once per frame with that frame's input. */
osk_status OSK_Update(osk_keyboard *osk, const osk_input *in);

/* Render with GX inside the caller's frame (between clearing and
   GX_CopyDisp). Assumes a full-screen viewport. Covers the whole screen.
   Clobbers projection, position matrix 0, vertex descriptors/format 0, TEV
   stages 0-1, TEV registers 0-1, texmap 0, channels, blending, alpha compare
   and Z mode; re-set them before your own drawing. The first call builds
   the textures (about 0.4 MB, kept until exit). */
void OSK_Draw(osk_keyboard *osk);

osk_status OSK_GetStatus(const osk_keyboard *osk);

/* Current text as UTF-8. The pointer stays valid until the next call that
   modifies the keyboard. */
const char *OSK_GetText(osk_keyboard *osk);
/* Copy UTF-8 text into buf (always NUL terminated, never splits a character).
   Returns the number of bytes written excluding the terminator. */
size_t OSK_CopyText(osk_keyboard *osk, char *buf, size_t size);
void OSK_SetText(osk_keyboard *osk, const char *utf8);
unsigned OSK_GetLength(const osk_keyboard *osk);

/* Return to OSK_STATUS_EDITING, keeping the text (e.g. after validation failed). */
void OSK_Resume(osk_keyboard *osk);

/* Self-contained blocking prompt for apps that render with the console or
   their own framebuffer. Takes over the display until OK or Cancel, then
   restores the framebuffer contents. WPAD_Init()/PAD_Init() must have been
   called. GX is initialised on first use unless OSK_FLAG_GX_READY is set.
   Returns OSK_STATUS_DONE or OSK_STATUS_CANCELLED. */
osk_status OSK_Prompt(const osk_config *cfg, GXRModeObj *rmode, void *xfb,
		      char *out, size_t out_size);

/* Optional sound effects (about 26 KB of compressed audio, linked only
   if one of these functions is used). OSK_InitSound() plays them through
   ASND (link -lasnd); pass true to have it call ASND_Init() as well.
   Each sound is decoded on first use (about 190 KB for all of them). */
bool OSK_InitSound(bool init_asnd);
void OSK_ShutdownSound(void);
void OSK_SetSoundVolume(u8 volume);		/* 0..255, default 255 */

/* For apps with their own mixer: native (big-endian) s16 mono samples. Vary the
   pitch randomly by up to +-jitter_cents for each play to sound like
   the system keyboard. The data stays valid until exit. */
typedef struct {
	const s16 *pcm;
	unsigned samples;
	unsigned rate;			/* Hz */
	unsigned jitter_cents;
} osk_sound_data;
bool OSK_GetSound(osk_sound sound, osk_sound_data *out);

/* The dictionary language matching the console settings, like the
   system keyboard: the language, with the American variants (en_us,
   fr_ca, es_la) in the USA, Brazil and Latin America areas and the
   European ones elsewhere. Links no dictionary data. */
osk_language OSK_SystemLanguage(void);
/* The built-in dictionary for a language, NULL for OSK_LANG_NONE.
   Using either of these links all nine dictionaries (about 630 KB);
   to keep a few, switch on OSK_SystemLanguage() yourself. */
const osk_dictionary *OSK_LanguageDictionary(osk_language lang);
const osk_dictionary *OSK_SystemDictionary(void);	/* of OSK_SystemLanguage() */

/* The system keyboard's letter layout for a language: US QWERTY for
   en_us (and OSK_LANG_NONE), the UK, Italian, Spanish and Dutch QWERTY
   layouts, German QWERTZ, French AZERTY (also for fr_ca). No data. */
osk_layout OSK_LanguageLayout(osk_language lang);
osk_layout OSK_SystemLayout(void);			/* of OSK_SystemLanguage() */

/* Set cfg->layout (unless it is OSK_LAYOUT_NUMPAD) and cfg->dictionary
   from the console language, like the system keyboard. Links all nine
   dictionaries, as OSK_SystemDictionary() does. */
void OSK_UseSystemLanguage(osk_config *cfg);

/* Fill d from .zsd / .znd files loaded by the app (e.g. from SD); the
   buffers must stay valid while a keyboard uses them. znd may be NULL.
   Returns false if the .zsd is not usable. */
bool OSK_DictionaryFromMemory(osk_dictionary *d, const char *name,
			      const void *zsd, size_t zsd_size,
			      const void *znd, size_t znd_size);

/* Release the unpacked built-in dictionaries. Destroy every keyboard
   that uses one first. */
void OSK_FreeDictionaries(void);

/* Prediction state, toggled by the prediction key. */
void OSK_SetPrediction(osk_keyboard *osk, bool on);
bool OSK_GetPrediction(const osk_keyboard *osk);
/* Suggestions for the word before the caret, best first. */
unsigned OSK_GetCandidateCount(const osk_keyboard *osk);
/* Copy candidate i as UTF-8 (like OSK_CopyText); returns 0 if there is none. */
size_t OSK_CopyCandidate(const osk_keyboard *osk, unsigned i, char *buf, size_t size);
/* Replace the word before the caret by candidate i, followed by a space. */
bool OSK_ChooseCandidate(osk_keyboard *osk, unsigned i);

/* UTF-8 helpers used by the library, exported for convenience. */
size_t OSK_UTF8Encode(uint32_t cp, char out[4]);
/* Decodes one code point; returns bytes consumed (0 at end of string). */
size_t OSK_UTF8Decode(const char *s, uint32_t *cp);

/* True if the built-in font can display (and therefore accept) cp. */
bool OSK_CanDisplay(uint32_t cp);

#ifdef __cplusplus
}
#endif

#endif /* __WIIOSK_H__ */
