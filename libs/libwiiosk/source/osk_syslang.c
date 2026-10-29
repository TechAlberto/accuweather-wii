/*-------------------------------------------------------------
osk_syslang.c -- the language and layout of the console settings
-------------------------------------------------------------*/
#include "osk_internal.h"

/* CONF_LANG_* and CONF_AREA_* values, kept here so that the mapping
   also builds (and is tested) on the desktop */
#define LANG_ENGLISH	1
#define LANG_GERMAN	2
#define LANG_FRENCH	3
#define LANG_SPANISH	4
#define LANG_ITALIAN	5
#define LANG_DUTCH	6
#define AREA_USA	1
#define AREA_BRA	4
#define AREA_LTN	10
#define REGION_US	1

osk_language osk_conf_language(int lang, int area, int region)
{
	bool americas = area >= 0 ? area == AREA_USA || area == AREA_BRA || area == AREA_LTN :
			region == REGION_US;

	switch (lang) {
	case LANG_ENGLISH:	return americas ? OSK_LANG_EN_US : OSK_LANG_EN_GB;
	case LANG_GERMAN:	return OSK_LANG_DE;
	case LANG_FRENCH:	return americas ? OSK_LANG_FR_CA : OSK_LANG_FR;
	case LANG_SPANISH:	return americas ? OSK_LANG_ES_LA : OSK_LANG_ES;
	case LANG_ITALIAN:	return OSK_LANG_IT;
	case LANG_DUTCH:	return OSK_LANG_NL;
	default:		return OSK_LANG_NONE;
	}
}

osk_layout OSK_LanguageLayout(osk_language lang)
{
	switch (lang) {
	case OSK_LANG_EN_GB:	return OSK_LAYOUT_QWERTY_UK;
	case OSK_LANG_DE:	return OSK_LAYOUT_QWERTZ;
	case OSK_LANG_FR:
	case OSK_LANG_FR_CA:	return OSK_LAYOUT_AZERTY;
	case OSK_LANG_ES:
	case OSK_LANG_ES_LA:	return OSK_LAYOUT_QWERTY_ES;
	case OSK_LANG_IT:	return OSK_LAYOUT_QWERTY_IT;
	case OSK_LANG_NL:	return OSK_LAYOUT_QWERTY_NL;
	default:		return OSK_LAYOUT_QWERTY;
	}
}

#ifdef GEKKO
_Static_assert(LANG_ENGLISH == CONF_LANG_ENGLISH && LANG_GERMAN == CONF_LANG_GERMAN &&
	       LANG_FRENCH == CONF_LANG_FRENCH && LANG_SPANISH == CONF_LANG_SPANISH &&
	       LANG_ITALIAN == CONF_LANG_ITALIAN && LANG_DUTCH == CONF_LANG_DUTCH &&
	       AREA_USA == CONF_AREA_USA && AREA_BRA == CONF_AREA_BRA &&
	       AREA_LTN == CONF_AREA_LTN && REGION_US == CONF_REGION_US, "CONF values");

osk_language OSK_SystemLanguage(void)
{
	CONF_Init();
	return osk_conf_language(CONF_GetLanguage(), CONF_GetArea(), CONF_GetRegion());
}

osk_layout OSK_SystemLayout(void)
{
	return OSK_LanguageLayout(OSK_SystemLanguage());
}
#endif
