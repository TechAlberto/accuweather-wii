/*-------------------------------------------------------------
wiiosk_input_wii.h -- feed wiiosk from libinput-wii

Header only: libinput-wii is not part of libwiiosk.a. Include this
in an app that already uses libinput-wii, then each frame:

	INPUT_Update();
	OSK_ReadInputWii(&in);
	OSK_Update(osk, &in);

Call INPUT_SetScreenSize() with the keyboard's screen_width and
screen_height so the pointer lines up. Do not call
OSK_InitUSBKeyboard() as well: libinput-wii already owns the
keyboard, and both would read it.

Mapping (on the controller mask libinput-wii folds every pad onto):
  A select, B backspace, D-pad focus, + OK, - Shift, 1 / 2 caret,
  HOME cancel, Nunchuk Z More, Nunchuk C space, left stick focus.
  The pointer is libinput-wii's cursor (Wii Remote IR or USB mouse);
  the left mouse button selects. USB keys type text; Enter, Esc,
  Backspace, Delete, arrows, Home and End edit.
  Note that libinput-wii folds GameCube Start onto HOME (cancel) and
  the Classic Controller's X / Y onto 2 / 1.

This software is provided 'as-is', without any express or implied
warranty. See LICENSE for the full zlib license text.
-------------------------------------------------------------*/
#ifndef __WIIOSK_INPUT_WII_H__
#define __WIIOSK_INPUT_WII_H__

#include <string.h>
#include <wiiuse/wpad.h>
#include <input_wii.h>

#include "wiiosk.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OSK_INPUT_WII_STICK	0.6f	/* stick deflection that counts as a direction */

static inline u32 osk_input_wii_map(u32 b)
{
	static const struct { u32 from, to; } map[] = {
		{ WPAD_BUTTON_A,	OSK_BTN_SELECT },
		{ WPAD_BUTTON_B,	OSK_BTN_BACKSPACE },
		{ WPAD_BUTTON_UP,	OSK_BTN_UP },
		{ WPAD_BUTTON_DOWN,	OSK_BTN_DOWN },
		{ WPAD_BUTTON_LEFT,	OSK_BTN_LEFT },
		{ WPAD_BUTTON_RIGHT,	OSK_BTN_RIGHT },
		{ WPAD_BUTTON_PLUS,	OSK_BTN_CONFIRM },
		{ WPAD_BUTTON_MINUS,	OSK_BTN_SHIFT },
		{ WPAD_BUTTON_1,	OSK_BTN_CARET_LEFT },
		{ WPAD_BUTTON_2,	OSK_BTN_CARET_RIGHT },
		{ WPAD_BUTTON_HOME,	OSK_BTN_CANCEL },
		{ INPUT_BUTTON_Z,	OSK_BTN_ALT },
		{ INPUT_BUTTON_C,	OSK_BTN_SPACE },
	};
	u32 out = 0;
	unsigned i;

	for (i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (b & map[i].from)
			out |= map[i].to;
	return out;
}

static inline void osk_input_wii_text(osk_input *in, uint32_t cp)
{
	if (in->text_count < OSK_INPUT_MAX_TEXT)
		in->text[in->text_count++] = cp;
}

/* One frame of input from libinput-wii; call after INPUT_Update(). */
static inline void OSK_ReadInputWii(osk_input *in)
{
	static u32 stick_prev;
	input_key_event ev;
	float sx, sy;
	u32 stick = 0;
	int px, py;

	memset(in, 0, sizeof(*in));

	/* controllers only: INPUT_ButtonsDown() would also contain the
	   keyboard map, so keys would fire twice */
	in->down = osk_input_wii_map(INPUT_PadsDown());
	in->held = osk_input_wii_map(INPUT_PadsHeld());

	INPUT_Stick(INPUT_STICK_LEFT, &sx, &sy, NULL);
	if (sx < -OSK_INPUT_WII_STICK) stick |= OSK_BTN_LEFT;
	if (sx > OSK_INPUT_WII_STICK)  stick |= OSK_BTN_RIGHT;
	if (sy > OSK_INPUT_WII_STICK)  stick |= OSK_BTN_UP;
	if (sy < -OSK_INPUT_WII_STICK) stick |= OSK_BTN_DOWN;
	in->held |= stick;
	in->down |= stick & ~stick_prev;
	stick_prev = stick;

	if (INPUT_CursorActive()) {
		INPUT_CursorPosition(&px, &py);
		in->pointer_valid = true;
		in->pointer_x = px;
		in->pointer_y = py;
	}
	if (INPUT_MouseButtonsPressed() & INPUT_MOUSE_LEFT)
		in->down |= OSK_BTN_SELECT;
	if (INPUT_MouseButtonsHeld() & INPUT_MOUSE_LEFT)
		in->held |= OSK_BTN_SELECT;

	while (INPUT_KeyPop(&ev)) {
		switch (ev.code) {
		case 0x28:					/* Enter */
		case 0x58: osk_input_wii_text(in, '\n'); continue;	/* keypad Enter */
		case 0x29: in->down |= OSK_BTN_CANCEL; continue;	/* Esc */
		case 0x2A: in->down |= OSK_BTN_BACKSPACE; continue;
		case 0x4C: in->down |= OSK_BTN_DELETE; continue;
		case 0x50: in->down |= OSK_BTN_CARET_LEFT; continue;
		case 0x4F: in->down |= OSK_BTN_CARET_RIGHT; continue;
		case 0x4A: in->down |= OSK_BTN_CARET_HOME; continue;
		case 0x4D: in->down |= OSK_BTN_CARET_END; continue;
		}
		if (ev.code >= 0x59 && ev.code <= 0x62)		/* keypad 1-9, 0 */
			osk_input_wii_text(in, ev.code == 0x62 ? '0' : '1' + ev.code - 0x59);
		else if (ev.code == 0x63)
			osk_input_wii_text(in, '.');
		else if (ev.ch >= 0x20 && ev.ch < 0x7F)
			osk_input_wii_text(in, (u8)ev.ch);
	}

	/* held keys repeat like the buttons (key presses do not repeat) */
	if (INPUT_KeyHeld(0x2A)) in->held |= OSK_BTN_BACKSPACE;
	if (INPUT_KeyHeld(0x4C)) in->held |= OSK_BTN_DELETE;
	if (INPUT_KeyHeld(0x50)) in->held |= OSK_BTN_CARET_LEFT;
	if (INPUT_KeyHeld(0x4F)) in->held |= OSK_BTN_CARET_RIGHT;
}

#ifdef __cplusplus
}
#endif

#endif /* __WIIOSK_INPUT_WII_H__ */
