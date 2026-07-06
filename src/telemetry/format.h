#pragma once
#include <Arduino.h>
#include "buffer.h"

// Pure formatting helpers for telemetry entries. MOVIDO de telemetry.cpp
// (buildRaw/buildJson/fieldValue/u64ToString/toHex) sem mudanca de logica —
// apenas devicePubkeyHex passa a ser parametro explicito em vez de vir de
// um ponteiro global (gId).

// Monta a string "raw" assinada pelo device: pubkey|seq|ts|0|campo=valor...
String telemetryBuildRaw(const TelemetryEntry& e, const String& devicePubkeyHex);

// Monta o JSON enviado ao servidor a partir da entry, do raw ja construido
// e da assinatura hex sobre esse raw.
String telemetryBuildJson(const TelemetryEntry& e, const String& devicePubkeyHex,
                           const String& raw, const String& sigHex);
