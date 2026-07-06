#include <Arduino.h>
#include <unity.h>
#include "identity/identity.h"
#include "i18n/i18n.h"

// Mnemonic BIP-39 de teste bem conhecida (entropia toda zero + checksum
// "about"). Usada em várias libs BIP-39 como vetor de teste canônico.
static const char* kValidMnemonicEn =
    "abandon abandon abandon abandon abandon abandon "
    "abandon abandon abandon abandon abandon about";

// ---------------------------------------------------------------------
// validateMnemonicForLanguage
// ---------------------------------------------------------------------

void test_validate_12_words_valid_en() {
  String error;
  const bool ok = validateMnemonicForLanguage(kValidMnemonicEn, LANG_EN, error);
  TEST_ASSERT_TRUE(ok);
}

void test_validate_11_words_word_count_error() {
  String error;
  const String elevenWords =
      "abandon abandon abandon abandon abandon abandon "
      "abandon abandon abandon abandon abandon";
  const bool ok = validateMnemonicForLanguage(elevenWords, LANG_EN, error);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_STRING("word_count", error.c_str());
}

void test_validate_word_outside_wordlist_error() {
  String error;
  // 12 tokens, mas o 6o nao existe na wordlist EN.
  const String badWord =
      "abandon abandon abandon abandon abandon notaword "
      "abandon abandon abandon abandon abandon about";
  const bool ok = validateMnemonicForLanguage(badWord, LANG_EN, error);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_STRING("wordlist", error.c_str());
}

void test_validate_normalization_spaces_and_uppercase() {
  String error;
  // Espacos duplos entre palavras + tudo em maiusculas.
  const String messy =
      "ABANDON  ABANDON ABANDON ABANDON ABANDON ABANDON "
      "ABANDON ABANDON  ABANDON ABANDON ABANDON ABOUT";
  const bool ok = validateMnemonicForLanguage(messy, LANG_EN, error);
  TEST_ASSERT_TRUE(ok);
}

// ---------------------------------------------------------------------
// analyzeMnemonicForLanguage
// ---------------------------------------------------------------------

void test_analyze_partial_prefix_last_word() {
  // 11 palavras completas + prefixo "abo" na 12a (sem espaco final), que
  // casa com "about"/"above" na wordlist EN -> partial com sugestoes.
  const String partial =
      "abandon abandon abandon abandon abandon abandon "
      "abandon abandon abandon abandon abandon abo";
  const MnemonicValidationResult result =
      analyzeMnemonicForLanguage(partial, LANG_EN);
  TEST_ASSERT_TRUE(result.partial);
  TEST_ASSERT_FALSE(result.complete);
  TEST_ASSERT_TRUE(result.suggestionCount >= 1);
}

// ---------------------------------------------------------------------
// importOwnerIdentity - GOLDEN
// ---------------------------------------------------------------------

void test_import_owner_identity_golden_pubkey() {
  DeviceIdentity id;
  const bool ok = importOwnerIdentity(id, kValidMnemonicEn);
  TEST_ASSERT_TRUE(ok);
  // GOLDEN: owner_public_key_hex derivado de SHA256(mnemonic) -> Ed25519.
  // Pino a saida de importOwnerIdentity() para kValidMnemonicEn ("abandon
  // x11 about"). Para regenerar: rode `pio test -e qemu`, copie o valor
  // ACTUAL da falha do TEST_ASSERT_EQUAL_STRING abaixo.
  TEST_ASSERT_EQUAL_STRING(
      "93a5f261984931e0df5c7434b16d468efb1953098d3cad4fa1506b9e052e7fc7",
      id.owner_public_key_hex.c_str());
}

// ---------------------------------------------------------------------
// signMessage - GOLDEN (identidade fixa do QEMU_EMULATOR)
// ---------------------------------------------------------------------

void test_sign_message_golden_signature() {
  // loadOrCreateIdentity() em build QEMU_EMULATOR retorna sempre a mesma
  // identidade de seed fixa (ver src/identity/identity.cpp).
  const DeviceIdentity id = loadOrCreateIdentity();
  const String sig = signMessage(id, "safrasense-test-message");
  TEST_ASSERT_EQUAL(128, sig.length());
  // GOLDEN: assinatura Ed25519 de "safrasense-test-message" com a
  // private_key fixa do QEMU_EMULATOR. Para regenerar: rode
  // `pio test -e qemu`, copie o valor ACTUAL da falha abaixo (a assinatura
  // Ed25519 e determinaistica para a mesma chave+mensagem).
  TEST_ASSERT_EQUAL_STRING(
      "385aeecd115aa3c1d221b7fc3ff485955b7fd94b653877d86c5e1901aa07627e"
      "e111dd65a689800e8456de4d4a8bcfb3590e3838dec4f6b0910633bf6f0b8407",
      sig.c_str());
}

// ---------------------------------------------------------------------
// loadOrCreateIdentity sob QEMU_EMULATOR - GOLDEN device_id
// ---------------------------------------------------------------------

void test_load_or_create_identity_fixed_device_id() {
  const DeviceIdentity id = loadOrCreateIdentity();
  // GOLDEN: public_key_hex derivado da kFixedSeed do QEMU_EMULATOR (ver
  // src/identity/identity.cpp). Esse e o mesmo device_id esperado no
  // raiznetd para o emulador. Para regenerar: rode `pio test -e qemu`,
  // copie o valor ACTUAL da falha abaixo.
  TEST_ASSERT_EQUAL_STRING(
      "66428465178bfab34f2a615b8902ac3ba942e8f3eeeecd2ffd7ef3083007df56",
      id.public_key_hex.c_str());
}

void test_save_then_reload_still_fixed_under_qemu() {
  // Sob QEMU_EMULATOR, saveIdentity() grava na NVS normalmente, mas
  // loadOrCreateIdentity() ignora o estado salvo e sempre retorna a
  // identidade fixa - garante device_id estavel entre boots no emulador.
  const DeviceIdentity id1 = loadOrCreateIdentity();
  saveIdentity(id1);
  const DeviceIdentity id2 = loadOrCreateIdentity();
  TEST_ASSERT_EQUAL_STRING(id1.public_key_hex.c_str(), id2.public_key_hex.c_str());
}

void setup() {
  delay(200);
  UNITY_BEGIN();
  RUN_TEST(test_validate_12_words_valid_en);
  RUN_TEST(test_validate_11_words_word_count_error);
  RUN_TEST(test_validate_word_outside_wordlist_error);
  RUN_TEST(test_validate_normalization_spaces_and_uppercase);
  RUN_TEST(test_analyze_partial_prefix_last_word);
  RUN_TEST(test_import_owner_identity_golden_pubkey);
  RUN_TEST(test_sign_message_golden_signature);
  RUN_TEST(test_load_or_create_identity_fixed_device_id);
  RUN_TEST(test_save_then_reload_still_fixed_under_qemu);
  UNITY_END();
}
void loop() {}
