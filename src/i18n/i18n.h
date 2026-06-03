#pragma once
#include <Arduino.h>

enum Language { LANG_EN, LANG_PT, LANG_ES, LANG_JA, LANG_ZH };

// Returns the translation for a specific key in the selected language.
String t(const char* key, Language lang);

// Helper to convert form strings ("0", "1", "2") into the Language enum.
Language docToLang(String val);
