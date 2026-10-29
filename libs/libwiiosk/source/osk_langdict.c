/*-------------------------------------------------------------
osk_langdict.c -- built-in dictionary by language

Its own object: it references every dictionary.
-------------------------------------------------------------*/
#include "osk_internal.h"

const osk_dictionary *OSK_LanguageDictionary(osk_language lang)
{
	switch (lang) {
	case OSK_LANG_EN_US:	return &osk_dict_en_us;
	case OSK_LANG_EN_GB:	return &osk_dict_en_gb;
	case OSK_LANG_DE:	return &osk_dict_de;
	case OSK_LANG_FR:	return &osk_dict_fr;
	case OSK_LANG_FR_CA:	return &osk_dict_fr_ca;
	case OSK_LANG_ES:	return &osk_dict_es;
	case OSK_LANG_ES_LA:	return &osk_dict_es_la;
	case OSK_LANG_IT:	return &osk_dict_it;
	case OSK_LANG_NL:	return &osk_dict_nl;
	default:		return NULL;
	}
}
