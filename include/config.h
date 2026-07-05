#pragma once

// ── Power pins (enabled before reading, disabled afterwards) ───────────────
#define PIN_POWER_TDS   25
#define PIN_POWER_DHT   26

// ── Sensor signal pins ────────────────────────────────────────────────────
#define PIN_DHT         4
#define PIN_TDS         34
#define PIN_BATTERY     35

// ── I2C — VL53L0X laser sensor ───────────────────────────────────────────
#define PIN_SDA         21
#define PIN_SCL         22
#define VL53_I2C_ADDR   0x29

// ── LED pins ──────────────────────────────────────────────────────────────
#define PIN_LED_RED     14
#define PIN_LED_YELLOW  12
#define PIN_LED_GREEN   13

// ── BOOT button (GPIO0) — available on every DevKit v1 ────────────────────
#define PIN_BOOT_BUTTON 0
#define BTN_SHORT_MS    2000    // hold 2s -> reconnect Wi-Fi
#define BTN_LONG_MS     5000    // hold 5s -> full reset

// ── Operation ─────────────────────────────────────────────────────────────
#ifndef TELEMETRY_INTERVAL_MS
#define TELEMETRY_INTERVAL_MS   60000   // 60s (o env wokwi sobrescreve via build_flags)
#endif
#define TELEMETRY_BUFFER_SIZE   50      // max readings in RAM
#define TELEMETRY_SEQ_BLOCK_SIZE 100    // reserves N seqs per NVS write

// ── Clock ─────────────────────────────────────────────────────────────────
#define NTP_SERVER_1        "pool.ntp.org"
#define NTP_SERVER_2        "time.google.com"
#define NTP_GMT_OFFSET_SEC  0
#define NTP_DAYLIGHT_SEC    0

// ── Default external server ───────────────────────────────────────────────
// These values are used only as defaults during first-time setup.
// "Arateki" is referenced only here; the rest of the code uses variables.
#define DEFAULT_SERVER_EXT_NAME  "Arateki"
#define DEFAULT_SERVER_EXT_URL   "https://raiznet.com/v1/telemetry"

// ── NVS namespaces (persistent flash storage) ─────────────────────────────
#define NVS_CONFIG_NS   "cfg"
#define NVS_IDENTITY_NS "ident"
#define NVS_TELEMETRY_NS "tel"

// ── Emulador QEMU (só existe no env `qemu`) ──────────────────────────────
#ifdef QEMU_EMULATOR
// 10.0.2.2 = host visto de dentro da rede slirp do QEMU.
#define EMU_SERVER_EXT_NAME   "raiznetd-public"
#define EMU_SERVER_EXT_URL    "http://10.0.2.2:3000/v1/telemetry"
#define EMU_SERVER_LOCAL_NAME "raiznetd-local"
#define EMU_SERVER_LOCAL_URL  "10.0.2.2:3001"
#endif
