#include <Arduino.h>
#include <unity.h>
#include <ArduinoJson.h>
#include "config.h"
#include "telemetry/format.h"
#include "telemetry/buffer.h"

// pubkey de teste: "ab" repetido 32x (64 chars hex), como pede o brief.
static String testPubkey() {
  String s;
  s.reserve(64);
  for (int i = 0; i < 32; i++) s += "ab";
  return s;
}

// ---------------------------------------------------------------------
// telemetryBuildRaw - GOLDEN
// ---------------------------------------------------------------------

void test_build_raw_golden_all_fields() {
  TelemetryEntry e{};
  e.seq            = 7;
  e.timestamp_ms   = 1700000000123ULL;
  e.ec             = 1400.4f;
  e.ph             = 6.204f;
  e.water_level    = 120;
  e.temp_ambient   = 24.5f;
  e.humidity       = 60.2f;
  e.confirmed_mask = 0;

  const String pubkey = testPubkey();
  const String raw = telemetryBuildRaw(e, pubkey);

  // GOLDEN calculado a mao a partir da logica movida (decimais 0,2,0,2,2):
  // ec=1400.4 -> "1400" | ph=6.204 -> "6.20" | waterLevel=120 -> "120"
  // | tempAmbient=24.5 -> "24.50" | humidity=60.2 -> "60.20"
  const String expected =
      pubkey +
      "|7|1700000000123|0|ec=1400|ph=6.20|waterLevel=120|tempAmbient=24.50|humidity=60.20";

  TEST_ASSERT_EQUAL_STRING(expected.c_str(), raw.c_str());
}

void test_build_raw_omits_nan_and_negative_fields() {
  TelemetryEntry e{};
  e.seq          = 1;
  e.timestamp_ms = 1700000000000ULL;
  e.ec           = NAN;
  e.ph           = 6.5f;
  e.water_level  = -1;  // sensor offline -> omitido
  e.temp_ambient = NAN;
  e.humidity     = NAN;

  const String raw = telemetryBuildRaw(e, "pk");
  TEST_ASSERT_EQUAL_STRING("pk|1|1700000000000|0|ph=6.50", raw.c_str());
}

// ---------------------------------------------------------------------
// telemetryBuildJson
// ---------------------------------------------------------------------

void test_build_json_fields_and_types() {
  TelemetryEntry e{};
  e.seq          = 7;
  e.timestamp_ms = 1700000000123ULL;
  e.ec           = 1400.4f;
  e.ph           = 6.204f;
  e.water_level  = 120;
  e.temp_ambient = 24.5f;
  e.humidity     = 60.2f;

  const String pubkey = testPubkey();
  const String raw    = telemetryBuildRaw(e, pubkey);
  const String sig    = "deadbeefcafe";
  const String json   = telemetryBuildJson(e, pubkey, raw, sig);

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  TEST_ASSERT_FALSE(err);

  JsonObject block = doc["blocks"][0];

  // seq e timestamp devem ser STRING no JSON (evita perda de precisao em
  // clientes que parseiam uint64 como double).
  TEST_ASSERT_TRUE(block["seq"].is<const char*>());
  TEST_ASSERT_EQUAL_STRING("7", block["seq"].as<const char*>());
  TEST_ASSERT_TRUE(block["timestamp"].is<const char*>());
  TEST_ASSERT_EQUAL_STRING("1700000000123", block["timestamp"].as<const char*>());

  TEST_ASSERT_EQUAL(0, block["keyVersion"].as<int>());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.2f, block["ph"]["plain"].as<float>());

  TEST_ASSERT_EQUAL_STRING(sig.c_str(), block["signature"].as<const char*>());

  // raw ecoado como hex do proprio raw.
  String rawHex;
  {
    static const char* hex = "0123456789abcdef";
    rawHex.reserve(raw.length() * 2);
    for (size_t i = 0; i < raw.length(); i++) {
      uint8_t b = (uint8_t)raw[i];
      rawHex += hex[b >> 4];
      rawHex += hex[b & 0x0f];
    }
  }
  TEST_ASSERT_EQUAL_STRING(rawHex.c_str(), block["raw"].as<const char*>());
}

// ---------------------------------------------------------------------
// buffer.h — add / nextPending / confirmServer / overflow
// Nota: seq reserva blocos em NVS (TELEMETRY_SEQ_BLOCK_SIZE), entao os
// testes toleram seq inicial != 0 — so a ordem relativa importa.
// ---------------------------------------------------------------------

void test_buffer_seq_increasing_and_confirm_clears_pending() {
  bufferInit();
  const uint32_t mask = 1u;  // um unico servidor (bit 0)

  bufferAdd(SensorData{});
  bufferAdd(SensorData{});
  bufferAdd(SensorData{});

  uint64_t prevSeq  = 0;
  bool     hasPrev  = false;

  for (int i = 0; i < 3; i++) {
    TelemetryEntry* e = bufferNextPending(mask);
    TEST_ASSERT_NOT_NULL(e);
    if (hasPrev) {
      TEST_ASSERT_TRUE(e->seq > prevSeq);
    }
    prevSeq = e->seq;
    hasPrev = true;

    bufferConfirmServer(e->seq, 0);  // confirma o unico bit da mask -> allDone
  }

  // Todas as 3 entries confirmadas para a mask -> nao ha mais pendencias.
  TEST_ASSERT_NULL(bufferNextPending(mask));
}

void test_buffer_overflow_caps_pending_count() {
  bufferInit();
  const uint32_t mask = 1u;

  for (int i = 0; i < TELEMETRY_BUFFER_SIZE + 5; i++) {
    bufferAdd(SensorData{});
  }

  TEST_ASSERT_EQUAL(TELEMETRY_BUFFER_SIZE, bufferTotal());
  TEST_ASSERT_TRUE(bufferPendingCount(mask) <= TELEMETRY_BUFFER_SIZE);
}

void setup() {
  delay(200);
  UNITY_BEGIN();
  RUN_TEST(test_build_raw_golden_all_fields);
  RUN_TEST(test_build_raw_omits_nan_and_negative_fields);
  RUN_TEST(test_build_json_fields_and_types);
  RUN_TEST(test_buffer_seq_increasing_and_confirm_clears_pending);
  RUN_TEST(test_buffer_overflow_caps_pending_count);
  UNITY_END();
}
void loop() {}
