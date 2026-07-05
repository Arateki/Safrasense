#include "identity.h"
#include "config.h"
#include <Preferences.h>
#include <WiFi.h>
#include <Ed25519.h>
#include <SHA256.h>
#include <esp_random.h>
#include <string.h>

static String bytesToHex(const uint8_t* data, size_t len) {
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    if (data[i] < 0x10) out += '0';
    out += String(data[i], HEX);
  }
  return out;
}

void saveIdentity(const DeviceIdentity& id) {
  Preferences p;
  p.begin(NVS_IDENTITY_NS, false);
  p.putBytes("privkey", id.private_key, 32);
  p.putBytes("own_priv", id.owner_private_key, 32);
  p.putString("mnemonic", id.mnemonic);
  p.putInt("lang", (int)id.lang);
  p.end();
}

#ifdef QEMU_EMULATOR
// Emulador: o QEMU não persiste NVS entre execuções. Identidade fixa mantém
// o device_id estável e o owner determinístico evita registro 400
// (ownerPubkey vazio antes do onboarding).
DeviceIdentity loadOrCreateIdentity() {
  DeviceIdentity id;
  id.mac = WiFi.macAddress();
  static const uint8_t kFixedSeed[32] = {
    'S', 'a', 'f', 'r', 'a', 's', 'e', 'n',
    's', 'e', '-', 'W', 'o', 'k', 'w', 'i',
    '-', 'd', 'e', 'v', '-', '0', '0', '0',
    '1', 0, 0, 0, 0, 0, 0, 0,
  };
  memcpy(id.private_key, kFixedSeed, 32);
  Ed25519::derivePublicKey(id.public_key, id.private_key);
  id.public_key_hex = bytesToHex(id.public_key, 32);
  importOwnerIdentity(id,
    "abandon abandon abandon abandon abandon abandon "
    "abandon abandon abandon abandon abandon about");
  id.lang = LANG_PT;
  return id;
}
#else
DeviceIdentity loadOrCreateIdentity() {
  DeviceIdentity id;
  id.mac = WiFi.macAddress();
  Preferences p;
  p.begin(NVS_IDENTITY_NS, false);

  if (p.getBytes("privkey", id.private_key, 32) != 32) {
    esp_fill_random(id.private_key, 32);
    p.putBytes("privkey", id.private_key, 32);
  }
  Ed25519::derivePublicKey(id.public_key, id.private_key);
  id.public_key_hex = bytesToHex(id.public_key, 32);

  id.mnemonic = p.getString("mnemonic", "");
  id.lang = (Language)p.getInt("lang", (int)LANG_PT);
  if (id.mnemonic.length() > 0) {
    p.getBytes("own_priv", id.owner_private_key, 32);
    Ed25519::derivePublicKey(id.owner_public_key, id.owner_private_key);
    id.owner_public_key_hex = bytesToHex(id.owner_public_key, 32);
  }
  p.end();
  return id;
}
#endif

static const char* const* wordlistForLanguage(Language lang) {
  return (lang == LANG_PT) ? BIP39_WORDLIST_PT :
         (lang == LANG_ES) ? BIP39_WORDLIST_ES :
         (lang == LANG_JA) ? BIP39_WORDLIST_JA :
         (lang == LANG_ZH) ? BIP39_WORDLIST_ZH :
         BIP39_WORDLIST_EN;
}

static String normalizeMnemonic(String mnemonic) {
  mnemonic.trim();
  String normalized = "";
  normalized.reserve(mnemonic.length());
  bool previousSpace = false;
  for (size_t i = 0; i < mnemonic.length(); i++) {
    const char c = mnemonic[i];
    const bool isSpace = c == ' ' || c == '\n' || c == '\r' || c == '\t';
    if (isSpace) {
      if (!previousSpace && normalized.length() > 0) {
        normalized += ' ';
        previousSpace = true;
      }
    } else {
      normalized += c;
      previousSpace = false;
    }
  }
  normalized.trim();
  normalized.toLowerCase();
  return normalized;
}

static bool wordInList(const String& word, const char* const* list) {
  String normalizedWord = word;
  normalizedWord.toLowerCase();
  for (int i = 0; i < 2048; i++) {
    String candidate = list[i];
    candidate.toLowerCase();
    if (normalizedWord.equals(candidate)) return true;
  }
  return false;
}

static bool wordPrefixInList(const String& prefix, const char* const* list) {
  String normalizedPrefix = prefix;
  normalizedPrefix.toLowerCase();
  for (int i = 0; i < 2048; i++) {
    String candidate = list[i];
    candidate.toLowerCase();
    if (candidate.startsWith(normalizedPrefix)) return true;
  }
  return false;
}

static int collectWordSuggestions(const String& prefix, const char* const* list, String* suggestions, int maxSuggestions) {
  String normalizedPrefix = prefix;
  normalizedPrefix.toLowerCase();
  int count = 0;
  for (int i = 0; i < 2048; i++) {
    String candidate = list[i];
    candidate.toLowerCase();
    if (candidate.startsWith(normalizedPrefix)) {
      if (count < maxSuggestions) {
        suggestions[count] = list[i];
      }
      count++;
    }
  }
  return count;
}

MnemonicValidationResult analyzeMnemonicForLanguage(String mnemonic, Language lang) {
  MnemonicValidationResult result;
  result.complete = false;
  result.partial = false;
  result.wordCount = 0;
  result.missingWords = 12;
  result.suggestionCount = 0;
  result.error = "";

  const bool hasTrailingSpace = mnemonic.length() > 0 &&
    (mnemonic[mnemonic.length() - 1] == ' ' ||
     mnemonic[mnemonic.length() - 1] == '\n' ||
     mnemonic[mnemonic.length() - 1] == '\r' ||
     mnemonic[mnemonic.length() - 1] == '\t');
  mnemonic = normalizeMnemonic(mnemonic);
  if (mnemonic.length() == 0) {
    result.error = "empty";
    return result;
  }

  const char* const* list = wordlistForLanguage(lang);
  int start = 0;
  int tokenCount = 0;
  while (start < (int)mnemonic.length()) {
    int end = mnemonic.indexOf(' ', start);
    if (end < 0) end = mnemonic.length();
    const String word = mnemonic.substring(start, end);
    tokenCount++;
    if (tokenCount > 12) {
      result.error = "word_count";
      result.missingWords = 0;
      return result;
    }
    const bool isLastWord = end == (int)mnemonic.length();
    const bool allowPrefix = isLastWord && !hasTrailingSpace;
    if (word.length() == 0) {
      result.error = "wordlist";
      return result;
    }
    if (wordInList(word, list)) {
      result.wordCount++;
    } else if (allowPrefix && wordPrefixInList(word, list)) {
      result.partial = true;
      result.suggestionCount = collectWordSuggestions(word, list, result.suggestions, 6);
    } else {
      result.error = "wordlist";
      return result;
    }
    start = end + 1;
  }

  result.missingWords = 12 - result.wordCount;
  result.complete = result.wordCount == 12 && !result.partial;
  result.partial = !result.complete && result.error.length() == 0;
  if (result.partial) {
    result.error = "word_count";
  }
  return result;
}

bool validateMnemonicForLanguage(String mnemonic, Language lang, String& error) {
  const MnemonicValidationResult result = analyzeMnemonicForLanguage(mnemonic, lang);
  error = result.error;
  return result.complete;
}

static String generateMnemonicFromEntropy(const uint8_t* entropy, Language lang) {
  uint8_t hash[32];
  SHA256 sha;
  sha.update(entropy, 16);
  sha.finalize(hash, 32);

  uint16_t indices[12];
  uint32_t buffer = 0;
  int bitCount = 0;
  int wordIdx = 0;

  for (int i = 0; i < 17; i++) {
    uint8_t byte = (i < 16) ? entropy[i] : (hash[0] & 0xF0);
    int bitsToCopy = (i < 16) ? 8 : 4;
    buffer = (buffer << bitsToCopy) | (byte >> (8 - bitsToCopy));
    bitCount += bitsToCopy;
    while (bitCount >= 11) {
      indices[wordIdx++] = (buffer >> (bitCount - 11)) & 0x7FF;
      bitCount -= 11;
    }
  }

  const char* const* list = wordlistForLanguage(lang);
  String res = "";
  for (int i = 0; i < 12; i++) {
    res += list[indices[i]];
    if (i < 11) res += " ";
  }
  return res;
}

void generateOwnerIdentity(DeviceIdentity& id, Language lang) {
  id.lang = lang;
  uint8_t entropy[16];
  esp_fill_random(entropy, 16);
  id.mnemonic = generateMnemonicFromEntropy(entropy, lang);
  
  SHA256 sha;
  sha.update((uint8_t*)id.mnemonic.c_str(), id.mnemonic.length());
  sha.finalize(id.owner_private_key, 32);
  Ed25519::derivePublicKey(id.owner_public_key, id.owner_private_key);
  id.owner_public_key_hex = bytesToHex(id.owner_public_key, 32);
}

bool importOwnerIdentity(DeviceIdentity& id, String mnemonic) {
  mnemonic = normalizeMnemonic(mnemonic);
  id.mnemonic = mnemonic;
  SHA256 sha;
  sha.update((uint8_t*)mnemonic.c_str(), mnemonic.length());
  sha.finalize(id.owner_private_key, 32);
  Ed25519::derivePublicKey(id.owner_public_key, id.owner_private_key);
  id.owner_public_key_hex = bytesToHex(id.owner_public_key, 32);
  return true;
}

String signMessage(const DeviceIdentity& id, const String& msg) {
  uint8_t sig[64];
  Ed25519::sign(sig, id.private_key, id.public_key, (const uint8_t*)msg.c_str(), msg.length());
  return bytesToHex(sig, 64);
}

void eraseIdentity() {
  Preferences p;
  p.begin(NVS_IDENTITY_NS, false);
  p.clear();
  p.end();
}
