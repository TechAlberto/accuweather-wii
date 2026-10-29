/*-------------------------------------------------------------
osk_sysdict.c -- the built-in dictionary of the console settings
-------------------------------------------------------------*/
#include "osk_internal.h"

const osk_dictionary *OSK_SystemDictionary(void)
{
	return OSK_LanguageDictionary(OSK_SystemLanguage());
}

void OSK_UseSystemLanguage(osk_config *cfg)
{
	osk_language lang = OSK_SystemLanguage();

	if (cfg->layout != OSK_LAYOUT_NUMPAD)
		cfg->layout = OSK_LanguageLayout(lang);
	cfg->dictionary = OSK_LanguageDictionary(lang);
}
