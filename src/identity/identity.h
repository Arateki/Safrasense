#pragma once
#include <Arduino.h>
#include "i18n/i18n.h"

struct DeviceIdentity {
  uint8_t public_key[32];
  uint8_t private_key[32];
  String  public_key_hex;

  // Owner identity (BIP-39)
  uint8_t owner_public_key[32];
  uint8_t owner_private_key[32];
  String  owner_public_key_hex;
  String  mnemonic; 
  Language lang;

  String  mac;
};

struct MnemonicValidationResult {
  bool complete;
  bool partial;
  int wordCount;
  int missingWords;
  int suggestionCount;
  String suggestions[6];
  String error;
};

extern const char* const BIP39_WORDLIST_EN[2048];
extern const char* const BIP39_WORDLIST_PT[2048];
extern const char* const BIP39_WORDLIST_ES[2048];
extern const char* const BIP39_WORDLIST_JA[2048];
extern const char* const BIP39_WORDLIST_ZH[2048];

DeviceIdentity loadOrCreateIdentity();
void generateOwnerIdentity(DeviceIdentity& id, Language lang);
bool importOwnerIdentity(DeviceIdentity& id, String mnemonic);
bool validateMnemonicForLanguage(String mnemonic, Language lang, String& error);
MnemonicValidationResult analyzeMnemonicForLanguage(String mnemonic, Language lang);
String signMessage(const DeviceIdentity& id, const String& msg);
void eraseIdentity();
void saveIdentity(const DeviceIdentity& id);
