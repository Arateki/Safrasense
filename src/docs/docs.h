#pragma once
#include <Arduino.h>
#include "i18n/i18n.h"

// Appends the documentation body content to `out` in the given language.
// Falls back to EN for any language not yet translated.
// Generates inner accordion sections only — no page wrapper or CSS.
void appendDocsContent(String& out, Language lang = LANG_PT);

// Returns a complete standalone HTML page for the captive portal /docs route.
String buildDocsPortalPage(Language lang = LANG_PT);
