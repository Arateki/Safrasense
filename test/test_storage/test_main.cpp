#include <Arduino.h>
#include <unity.h>
#include "config.h"
#include "storage/storage.h"

// Sob QEMU_EMULATOR, o primeiro boot (config apagada) usa os defaults
// EMU_SERVER_* (ver src/storage/storage.cpp), nunca o servidor de producao.

void test_erase_then_load_gives_emu_defaults() {
  eraseConfig();
  DeviceConfig cfg = loadConfig();

  TEST_ASSERT_TRUE(cfg.device_name.startsWith("Safrasense-aqua-"));

  TEST_ASSERT_EQUAL(1, (int)cfg.servers_external.size());
  TEST_ASSERT_EQUAL_STRING(EMU_SERVER_EXT_NAME, cfg.servers_external[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING(EMU_SERVER_EXT_URL, cfg.servers_external[0].url.c_str());

  TEST_ASSERT_EQUAL(1, (int)cfg.servers_local.size());
  TEST_ASSERT_EQUAL_STRING(EMU_SERVER_LOCAL_NAME, cfg.servers_local[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING(EMU_SERVER_LOCAL_URL, cfg.servers_local[0].url.c_str());
}

void test_save_then_load_roundtrip() {
  DeviceConfig cfg;
  cfg.device_name = "meu-device-teste";
  cfg.servers_external.push_back({ "ext1", "https://ext1.example/v1/telemetry" });
  cfg.servers_local.push_back({ "loc1", "192.168.0.10:3001" });

  saveConfig(cfg);
  DeviceConfig loaded = loadConfig();

  TEST_ASSERT_EQUAL_STRING("meu-device-teste", loaded.device_name.c_str());

  TEST_ASSERT_EQUAL(1, (int)loaded.servers_external.size());
  TEST_ASSERT_EQUAL_STRING("ext1", loaded.servers_external[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("https://ext1.example/v1/telemetry", loaded.servers_external[0].url.c_str());

  TEST_ASSERT_EQUAL(1, (int)loaded.servers_local.size());
  TEST_ASSERT_EQUAL_STRING("loc1", loaded.servers_local[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("192.168.0.10:3001", loaded.servers_local[0].url.c_str());
}

void test_erase_after_save_returns_emu_defaults_again() {
  eraseConfig();
  DeviceConfig cfg = loadConfig();

  TEST_ASSERT_EQUAL(1, (int)cfg.servers_external.size());
  TEST_ASSERT_EQUAL_STRING(EMU_SERVER_EXT_NAME, cfg.servers_external[0].name.c_str());
  TEST_ASSERT_EQUAL(1, (int)cfg.servers_local.size());
  TEST_ASSERT_EQUAL_STRING(EMU_SERVER_LOCAL_NAME, cfg.servers_local[0].name.c_str());
}

void setup() {
  delay(200);
  UNITY_BEGIN();
  RUN_TEST(test_erase_then_load_gives_emu_defaults);
  RUN_TEST(test_save_then_load_roundtrip);
  RUN_TEST(test_erase_after_save_returns_emu_defaults_again);
  UNITY_END();
}
void loop() {}
