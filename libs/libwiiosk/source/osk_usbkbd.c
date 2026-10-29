/*-------------------------------------------------------------
osk_usbkbd.c -- USB keyboard input through libwiikeyboard

Kept in its own object so that only apps calling
OSK_InitUSBKeyboard() need to link -lwiikeyboard.
-------------------------------------------------------------*/
#include "osk_internal.h"

#include <wiikeyboard/keyboard.h>

extern void (*osk_usb_poll)(osk_input *in);

static void add_button(osk_input *in, u32 bit)
{
	in->down |= bit;
}

static void add_text(osk_input *in, uint32_t cp)
{
	if (in->text_count < OSK_INPUT_MAX_TEXT)
		in->text[in->text_count++] = cp;
}

static void usb_poll(osk_input *in)
{
	keyboard_event ev;

	while (KEYBOARD_GetEvent(&ev) > 0) {
		if (ev.type != KEYBOARD_PRESSED)
			continue;

		switch (ev.symbol) {
		case KS_Return:
		case KS_KP_Enter:	add_text(in, '\n'); continue;	/* line break or OK */
		case KS_Escape:		add_button(in, OSK_BTN_CANCEL); continue;
		case KS_BackSpace:	add_button(in, OSK_BTN_BACKSPACE); continue;
		case KS_Delete:
		case KS_KP_Delete:	add_button(in, OSK_BTN_DELETE); continue;
		case KS_Left:
		case KS_KP_Left:	add_button(in, OSK_BTN_CARET_LEFT); continue;
		case KS_Right:
		case KS_KP_Right:	add_button(in, OSK_BTN_CARET_RIGHT); continue;
		case KS_Home:
		case KS_KP_Home:	add_button(in, OSK_BTN_CARET_HOME); continue;
		case KS_End:
		case KS_KP_End:		add_button(in, OSK_BTN_CARET_END); continue;
		}

		if (ev.symbol >= KS_KP_Space && ev.symbol <= KS_KP_Equal)
			add_text(in, ev.symbol & 0xFF);	/* keypad maps onto ASCII */
		else if (ev.symbol >= 0x20 && ev.symbol <= 0xFF && ev.symbol != 0x7F)
			add_text(in, ev.symbol);	/* Latin-1 keysyms equal code points */
	}
}

bool OSK_InitUSBKeyboard(void)
{
	if (osk_usb_poll)
		return true;
	if (KEYBOARD_Init(NULL) < 0)
		return false;
	osk_usb_poll = usb_poll;
	return true;
}

void OSK_ShutdownUSBKeyboard(void)
{
	if (!osk_usb_poll)
		return;
	osk_usb_poll = NULL;
	KEYBOARD_Deinit();
}
