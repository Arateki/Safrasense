#include <Arduino.h>
#include <unity.h>
#include "i18n/i18n.h"

// Inventario de todas as chaves de traducao conhecidas por t() em
// src/i18n/i18n.cpp (extraidas via
//   grep -o 'strcmp(key, "[a-z_]*")' src/i18n/i18n.cpp | sort -u
// ). Qualquer chave usada pelo firmware (ver src/http_local, src/main.cpp
// etc via t("...")) esta contida neste conjunto. Para regenerar apos
// alterar i18n.cpp, rode o grep acima e atualize esta lista.
static const char* const kAllKeys[] = {
    "add_arateki_server",
    "add_other_server",
    "add_server",
    "advanced_settings",
    "backup_methods_hint",
    "connect_raiznet",
    "copied_code",
    "copy_code",
    "create_tab",
    "external_servers_title",
    "ext_server",
    "generate_btn",
    "identity_title",
    "import_btn",
    "local_server_placeholder",
    "local_servers_title",
    "loc_server",
    "master_key",
    "optional",
    "owner_id",
    "owner_secret",
    "pub_server_name",
    "recover_hint",
    "recover_qr",
    "recover_tab",
    "recover_words",
    "reroll_seed",
    "save_qr",
    "security_warn",
    "sensor_name",
    "servers_section",
    "server_url_placeholder",
    "setup_title",
};
static const size_t kAllKeysCount = sizeof(kAllKeys) / sizeof(kAllKeys[0]);

static void assertAllKeysNonEmptyForLang(Language lang) {
  for (size_t i = 0; i < kAllKeysCount; i++) {
    const String value = t(kAllKeys[i], lang);
    if (value.length() == 0) {
      // So loga em caso de falha, para nao inundar a saida serial.
      TEST_FAIL_MESSAGE(kAllKeys[i]);
    }
  }
}

void test_i18n_en_all_keys_non_empty() { assertAllKeysNonEmptyForLang(LANG_EN); }
void test_i18n_pt_all_keys_non_empty() { assertAllKeysNonEmptyForLang(LANG_PT); }
void test_i18n_es_all_keys_non_empty() { assertAllKeysNonEmptyForLang(LANG_ES); }
void test_i18n_ja_all_keys_non_empty() { assertAllKeysNonEmptyForLang(LANG_JA); }
void test_i18n_zh_all_keys_non_empty() { assertAllKeysNonEmptyForLang(LANG_ZH); }

void setup() {
  delay(200);
  UNITY_BEGIN();
  RUN_TEST(test_i18n_en_all_keys_non_empty);
  RUN_TEST(test_i18n_pt_all_keys_non_empty);
  RUN_TEST(test_i18n_es_all_keys_non_empty);
  RUN_TEST(test_i18n_ja_all_keys_non_empty);
  RUN_TEST(test_i18n_zh_all_keys_non_empty);
  UNITY_END();
}
void loop() {}
