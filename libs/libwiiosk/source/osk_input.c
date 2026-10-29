/*-------------------------------------------------------------
osk_input.c -- Wii Remote, Classic Controller and GameCube pad input
-------------------------------------------------------------*/
#include <string.h>
#include <wiiuse/wpad.h>

#include "osk_internal.h"

/* Set by OSK_InitUSBKeyboard() (osk_usbkbd.c) so that apps which do not
   use a USB keyboard need not link libwiikeyboard. */
void (*osk_usb_poll)(osk_input *in) = NULL;

static const struct { u32 from, to; } wpad_map[] = {
	{ WPAD_BUTTON_A,		OSK_BTN_SELECT },
	{ WPAD_BUTTON_B,		OSK_BTN_BACKSPACE },
	{ WPAD_BUTTON_UP,		OSK_BTN_UP },
	{ WPAD_BUTTON_DOWN,		OSK_BTN_DOWN },
	{ WPAD_BUTTON_LEFT,		OSK_BTN_LEFT },
	{ WPAD_BUTTON_RIGHT,		OSK_BTN_RIGHT },
	{ WPAD_BUTTON_PLUS,		OSK_BTN_CONFIRM },
	{ WPAD_BUTTON_MINUS,		OSK_BTN_SHIFT },
	{ WPAD_BUTTON_1,		OSK_BTN_CARET_LEFT },
	{ WPAD_BUTTON_2,		OSK_BTN_CARET_RIGHT },
	{ WPAD_BUTTON_HOME,		OSK_BTN_CANCEL },
	{ WPAD_CLASSIC_BUTTON_A,	OSK_BTN_SELECT },
	{ WPAD_CLASSIC_BUTTON_B,	OSK_BTN_BACKSPACE },
	{ WPAD_CLASSIC_BUTTON_UP,	OSK_BTN_UP },
	{ WPAD_CLASSIC_BUTTON_DOWN,	OSK_BTN_DOWN },
	{ WPAD_CLASSIC_BUTTON_LEFT,	OSK_BTN_LEFT },
	{ WPAD_CLASSIC_BUTTON_RIGHT,	OSK_BTN_RIGHT },
	{ WPAD_CLASSIC_BUTTON_PLUS,	OSK_BTN_CONFIRM },
	{ WPAD_CLASSIC_BUTTON_MINUS,	OSK_BTN_SHIFT },
	{ WPAD_CLASSIC_BUTTON_FULL_L,	OSK_BTN_CARET_LEFT },
	{ WPAD_CLASSIC_BUTTON_FULL_R,	OSK_BTN_CARET_RIGHT },
	{ WPAD_CLASSIC_BUTTON_X,	OSK_BTN_SPACE },
	{ WPAD_CLASSIC_BUTTON_Y,	OSK_BTN_ALT },
	{ WPAD_CLASSIC_BUTTON_HOME,	OSK_BTN_CANCEL },
};

static const struct { u32 from, to; } pad_map[] = {
	{ PAD_BUTTON_A,		OSK_BTN_SELECT },
	{ PAD_BUTTON_B,		OSK_BTN_BACKSPACE },
	{ PAD_BUTTON_UP,	OSK_BTN_UP },
	{ PAD_BUTTON_DOWN,	OSK_BTN_DOWN },
	{ PAD_BUTTON_LEFT,	OSK_BTN_LEFT },
	{ PAD_BUTTON_RIGHT,	OSK_BTN_RIGHT },
	{ PAD_BUTTON_START,	OSK_BTN_CONFIRM },
	{ PAD_BUTTON_Y,		OSK_BTN_SHIFT },
	{ PAD_BUTTON_X,		OSK_BTN_SPACE },
	{ PAD_TRIGGER_Z,	OSK_BTN_ALT },
	{ PAD_TRIGGER_L,	OSK_BTN_CARET_LEFT },
	{ PAD_TRIGGER_R,	OSK_BTN_CARET_RIGHT },
};

#define MAP_BUTTONS(table, bits) ({ \
	u32 _r = 0; unsigned _i; \
	for (_i = 0; _i < sizeof(table) / sizeof(table[0]); _i++) \
		if ((bits) & table[_i].from) _r |= table[_i].to; \
	_r; })

#define STICK_THRESHOLD	48

static u32 stick_prev[4];

void OSK_ReadInput(osk_input *in, int wpad_chan, int pad_chan)
{
	memset(in, 0, sizeof(*in));

	if (wpad_chan >= 0) {
		WPADData *wd = WPAD_Data(wpad_chan);

		in->down |= MAP_BUTTONS(wpad_map, WPAD_ButtonsDown(wpad_chan));
		in->held |= MAP_BUTTONS(wpad_map, WPAD_ButtonsHeld(wpad_chan));
		if (wd && wd->ir.valid) {
			in->pointer_valid = true;
			in->pointer_x = wd->ir.x;
			in->pointer_y = wd->ir.y;
		}
	}

	if (pad_chan >= 0 && pad_chan < 4) {
		s8 sx = PAD_StickX(pad_chan), sy = PAD_StickY(pad_chan);
		u32 stick = 0;

		in->down |= MAP_BUTTONS(pad_map, PAD_ButtonsDown(pad_chan));
		in->held |= MAP_BUTTONS(pad_map, PAD_ButtonsHeld(pad_chan));

		if (sx < -STICK_THRESHOLD) stick |= OSK_BTN_LEFT;
		if (sx > STICK_THRESHOLD)  stick |= OSK_BTN_RIGHT;
		if (sy > STICK_THRESHOLD)  stick |= OSK_BTN_UP;
		if (sy < -STICK_THRESHOLD) stick |= OSK_BTN_DOWN;
		in->held |= stick;
		in->down |= stick & ~stick_prev[pad_chan];
		stick_prev[pad_chan] = stick;
	}

	if (osk_usb_poll)
		osk_usb_poll(in);
}
