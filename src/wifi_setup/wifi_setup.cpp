#include "wifi_setup.h"
#include "config.h"
#include "leds/leds.h"
#include "identity/identity.h"
#include "identity/qr_decode.h"
#include "i18n/i18n.h"
#include "logging/logging.h"
#include "docs/docs.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <qrcode.h>
#include <string.h>
#include <time.h>

static String mdnsName;

static String htmlEscape(const String& value) {
  String out;
  out.reserve(value.length());
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '"') out += F("&quot;");
    else if (c == '\'') out += F("&#39;");
    else out += c;
  }
  return out;
}

static String jsonEscape(const String& value) {
  String out;
  out.reserve(value.length());
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '\\') out += F("\\\\");
    else if (c == '"') out += F("\\\"");
    else if (c == '\n') out += F("\\n");
    else if (c == '\r') out += F("\\r");
    else out += c;
  }
  return out;
}

static String serverListJson(const std::vector<ServerEntry>& servers) {
  JsonDocument doc;
  JsonArray list = doc.to<JsonArray>();
  for (const auto& server : servers) {
    if (server.name.length() == 0 || server.url.length() == 0) continue;
    JsonObject item = list.add<JsonObject>();
    item["name"] = server.name;
    item["url"] = server.url;
  }
  String json;
  serializeJson(doc, json);
  return json;
}

static void parseServerListJson(const String& json, std::vector<ServerEntry>& out, const char* fallbackName) {
  out.clear();
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok || !doc.is<JsonArray>()) {
    return;
  }
  for (JsonObject item : doc.as<JsonArray>()) {
    if (out.size() >= 16) break;
    String name = item["name"] | fallbackName;
    String url = item["url"] | "";
    name.trim();
    url.trim();
    if (url.length() == 0) continue;
    if (name.length() == 0) name = fallbackName;
    out.push_back({ name, url });
  }
}

static String renderMnemonicQrBits(const String& mnemonic, uint8_t& qrSize) {
  static const uint8_t minQrVersion = 6;
  static const uint8_t maxQrVersion = 12;
  static const uint16_t byteCapacityByVersion[] = {
    0, 17, 32, 53, 78, 106, 134, 154, 192, 230, 271, 321, 367
  };
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(maxQrVersion)];
  uint8_t selectedVersion = 0;
  const uint16_t payloadLength = mnemonic.length();

  for (uint8_t version = minQrVersion; version <= maxQrVersion; version++) {
    if (payloadLength > byteCapacityByVersion[version]) {
      continue;
    }
    memset(qrcodeData, 0, sizeof(qrcodeData));
    if (qrcode_initText(&qrcode, qrcodeData, version, ECC_LOW, mnemonic.c_str()) == 0) {
      selectedVersion = version;
      break;
    }
  }

  if (selectedVersion == 0) {
    qrSize = 0;
    return "";
  }

  LOG_INFOF("qr", "generated mnemonic QR version=%u size=%u payload_len=%u\n", selectedVersion, qrcode.size, (unsigned)mnemonic.length());
  qrSize = qrcode.size;

  String bits;
  bits.reserve(qrcode.size * qrcode.size);
  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      bits += qrcode_getModule(&qrcode, x, y) ? '1' : '0';
    }
  }
  return bits;
}

static String renderIdentitySectionHtml(const DeviceIdentity& id) {
  return
    "<div class='ident-section'>"
      "<div class='identity-tabs'>"
        "<button type='button' class='identity-tab is-active' data-tab='create' data-i18n='create_tab'>" + htmlEscape(t("create_tab", id.lang)) + "</button>"
        "<button type='button' class='identity-tab' data-tab='recover' data-i18n='recover_tab'>" + htmlEscape(t("recover_tab", id.lang)) + "</button>"
      "</div>"
      "<div class='identity-pane is-active' data-pane='create'>"
        "<button type='button' id='reroll-mnemonic' class='reroll-seed'>" + htmlEscape(t("reroll_seed", id.lang)) + "</button>"
        "<div class='eyebrow'>" + t("master_key", id.lang) + "</div>"
        "<div id='mnemonic-code' class='mnemonic-box' data-mnemonic='" + htmlEscape(id.mnemonic) + "'>" + htmlEscape(id.mnemonic) + "</div>"
        "<div class='identity-actions'>"
          "<button type='button' id='copy-mnemonic' class='btn' data-label='" + htmlEscape(t("copy_code", id.lang)) + "' data-done='" + htmlEscape(t("copied_code", id.lang)) + "'>" + htmlEscape(t("copy_code", id.lang)) + "</button>"
          "<button type='button' id='save-mnemonic-qr' class='btn'>" + htmlEscape(t("save_qr", id.lang)) + "</button>"
        "</div>"
        "<div class='qr-panel'>"
          "<canvas id='mnemonic-qr' class='qr-canvas' width='330' height='330' data-size='0' data-bits='' aria-label='QR code'></canvas>"
          "<div class='backup-hint'>" + htmlEscape(t("backup_methods_hint", id.lang)) + "</div>"
        "</div>"
        "<div class='warn-box'>" + t("security_warn", id.lang) + "</div>"
      "</div>"
      "<div class='identity-pane identity-recover' data-pane='recover'>"
        "<label for='import_mnemonic' data-i18n='recover_words'>" + htmlEscape(t("recover_words", id.lang)) + "</label>"
        "<div id='mnemonic-suggestions' class='word-suggestions' aria-live='polite'></div>"
        "<textarea id='import_mnemonic' name='import_mnemonic' maxlength='512' autocomplete='off' rows='3'></textarea>"
        "<label class='btn' for='import-qr' data-i18n='recover_qr'>" + htmlEscape(t("recover_qr", id.lang)) + "</label>"
        "<input id='import-qr' type='file' accept='image/*'>"
        "<div id='qr-import-status' class='backup-hint' data-i18n='recover_hint'>" + htmlEscape(t("recover_hint", id.lang)) + "</div>"
      "</div>"
    "</div>";
}

// HTML/JS for displaying seed words and safety instructions.
const char IDENTITY_CSS[] PROGMEM = R"rawliteral(
<style>
  :root { --bg:#f4f1ea; --fg:#1d231e; --fg-2:#46493d; --fg-3:#6d6a5f; --pri:#1a3a28; --line:#d8d2bf; --input-line:#d8d2bf; --pap:#f7f1de; --bad:#a83a2a; --warn:#9b6b12; --ok:#2f7d45; --btn-fg:#f4f1ea; --aqua:#9ed8ff; --mnemonic:#1a3a28; }
  [data-theme="dark"] { --bg:#0d1310; --fg:#d8e3d4; --fg-2:#b3c2af; --fg-3:#9ead99; --pri:#2d6e4a; --line:#20281f; --input-line:#9ead99; --pap:#14201a; --bad:#d36e63; --warn:#d2aa4f; --ok:#7fd08d; --btn-fg:#f4f1ea; --aqua:#a8dcff; --mnemonic:#c7efd5; }
  body { background:var(--bg); color:var(--fg); font-family:-apple-system,system-ui,sans-serif; text-align:left; padding:20px; margin:0; transition: background 0.2s, color 0.2s; display:flex; justify-content:center; }
  body::before { content:''; position:fixed; top:0; left:0; right:0; height:68px; background:var(--bg); z-index:89; pointer-events:none; }
  .wrap { width:100%; max-width:400px; margin:0 auto; text-align:left; position:relative; padding-top:54px; }
  h1 { font-family:Georgia,serif; font-size:24px; font-weight:normal; margin:0 0 20px; color:var(--fg); line-height:1.2; text-align:left; }
  .eyebrow { font-size:10px; letter-spacing:0.18em; text-transform:uppercase; color:var(--fg-3); margin-bottom:8px; font-weight:650; display:block; }
  .brand-aqua { color:var(--aqua); }
  .portal-brand { position:fixed; top:20px; left:50%; transform:translateX(-50%); width:calc(100% - 40px); max-width:400px; height:42px; display:flex; align-items:center; white-space:nowrap; overflow:hidden; z-index:90; background:var(--bg); }
  .portal-brand .eyebrow { margin:0; font-size:12px; font-weight:800; letter-spacing:.16em; }
  input, select, textarea { display:block; width:100%; box-sizing:border-box; padding:14px; border:1px solid var(--input-line); background:transparent; font-family:monospace; font-size:14px; margin-bottom:16px; color:var(--fg); border-radius:4px; appearance:none; }
  input[type="checkbox"] { appearance:auto; -webkit-appearance:checkbox; width:18px; height:18px; padding:0; margin:0 8px 0 0; accent-color:var(--pri); vertical-align:middle; }
  input[type="checkbox"] + label, input[type="checkbox"] + span { display:inline-block; vertical-align:middle; margin:0; }
  input:focus, select:focus, textarea:focus { border-color:var(--pri); outline:none; }
  button, input[type="submit"], input[type="button"], .btn { position:relative; display:block; background:var(--pri); color:var(--btn-fg) !important; border:1px solid var(--pri); font-family:-apple-system,sans-serif; font-size:13px; font-weight:500; letter-spacing:0.04em; padding:8px 16px; cursor:pointer; width:100%; margin-bottom:12px; border-radius:4px; text-transform:uppercase; text-decoration:none; text-align:center; box-sizing:border-box; overflow:hidden; transition:transform .08s ease; }
  button:hover, input[type="submit"]:hover, input[type="button"]:hover, .btn:hover { opacity:0.9; }
  button:active, input[type="submit"]:active, input[type="button"]:active, .btn:active { transform:scale(.96); opacity:1; }
  #loader-overlay { display:none; position:fixed; top:0; left:0; width:100%; height:100%; background:var(--bg); opacity:0.85; z-index:9999; }
  #loader-overlay::after { content:"•••"; position:absolute; left:50%; top:50%; transform:translate(-50%,-50%); color:var(--fg); font-size:32px; letter-spacing:4px; animation:blink 1.4s infinite both; }
  @keyframes blink { 0% { opacity:.2; } 20% { opacity:1; } 100% { opacity:.2; } }
  body.is-loading { pointer-events:none !important; overflow:hidden; }
  body.is-loading #loader-overlay { display:block; }
  /* WiFiManager default overrides */
  div[style*="text-align:center"], div[style*="text-align: center"] { text-align:left !important; }
  div.c { display:none; } /* hide default header */
  hr { display:none !important; } /* hide WiFiManager section dividers */
  .q { color:var(--fg-3); } /* fix dark mode list text */
  a { color:var(--pri); text-decoration:none; }
  a:hover { text-decoration:underline; }
  .msg { padding:14px; background:var(--pap); border:1px solid var(--line); border-radius:4px; margin-bottom:16px; font-size:12px; color:var(--fg-2); line-height:1.5; }
  /* Ident section */
  .ident-section { border:1px solid var(--line); background:var(--pap); padding:12px 16px 16px; margin:20px 0; border-radius:4px; position:relative; }
  .identity-tabs { display:flex; align-items:flex-end; justify-content:flex-start; gap:8px; margin:-4px 0 8px; border-bottom:1px solid var(--line); }
  .identity-tab { display:inline-flex; width:auto; margin:0 0 -1px; padding:4px 9px 5px; background:transparent !important; color:var(--fg-3) !important; border:1px solid var(--line) !important; border-bottom:2px solid var(--line) !important; border-radius:4px 4px 0 0; font-size:10px; font-weight:600; letter-spacing:.08em; }
  .identity-tab.is-active { color:var(--fg) !important; border-color:var(--pri) !important; border-bottom-width:3px !important; font-weight:800; }
  .identity-pane { display:none; }
  .identity-pane.is-active { display:block; }
  .mnemonic-box { font-family:monospace; font-size:14px; line-height:1.6; word-spacing:6px; color:var(--mnemonic); margin-top:8px; font-weight:650; }
  .identity-actions { display:grid; grid-template-columns:1fr 1fr; gap:8px; margin-top:12px; }
  .identity-actions .btn { margin:0; padding:5px 8px; font-size:11px; }
  .identity-actions .btn:hover { opacity:1; }
  .reroll-seed { position:absolute; top:10px; right:10px; width:auto; margin:0; padding:3px 9px; font-size:10px; }
  .reroll-seed:hover { opacity:.9; }
  .word-suggestions { min-height:30px; height:30px; display:flex; align-items:flex-start; flex-wrap:nowrap; gap:5px; overflow:hidden; margin:-2px 0 8px; padding-bottom:2px; box-sizing:border-box; }
  .word-suggestion { display:inline-flex !important; align-items:center; width:auto !important; max-width:120px; min-height:22px; height:22px; margin:0 !important; padding:2px 7px !important; background:transparent !important; color:var(--fg) !important; border:1px solid var(--line) !important; border-radius:4px; font-family:monospace !important; font-size:11px !important; font-weight:650 !important; line-height:1 !important; letter-spacing:0 !important; text-transform:none !important; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
  .word-suggestion:active { transform:scale(.96); }
  .identity-recover textarea { min-height:84px; max-width:100%; resize:vertical; overflow:hidden; line-height:1.45; white-space:pre-wrap; }
  .identity-recover textarea.is-error { border-color:var(--bad); color:var(--bad); }
  .identity-recover .backup-hint.is-error { color:var(--bad); }
  .identity-recover .backup-hint.is-warn { color:var(--warn); }
  .identity-recover .backup-hint.is-success { color:var(--ok); font-weight:650; }
  .identity-recover .backup-hint.is-success::before { content:"✓ "; }
  .identity-recover input[type="file"] { display:none; }
  input[type="submit"]:disabled, button:disabled { opacity:.45; cursor:not-allowed; transform:none !important; }
  .identity-recover .btn { padding:8px 10px; font-size:11px; margin-bottom:8px; }
  .qr-panel { margin-top:12px; padding:12px; border:1px solid var(--line); border-radius:4px; background:rgba(255,255,255,.28); }
  [data-theme="dark"] .qr-panel { background:rgba(255,255,255,.04); }
  .qr-canvas { display:block; width:260px; height:260px; max-width:100%; margin:0 auto 12px; image-rendering:pixelated; border:4px solid #fff; box-sizing:border-box; background:#fff; }
  .warn-box { font-size:11px; color:var(--bad); margin-top:12px; line-height:1.4; border-left:2px solid var(--bad); padding-left:10px; }
  .backup-hint { font-size:11px; color:var(--fg-2); margin-top:12px; line-height:1.4; }
  .save-disabled-reason { display:none; color:var(--bad); font-size:11px; line-height:1.4; margin:-2px 0 10px; }
  .save-disabled-reason.is-warn { color:var(--warn); }
  .save-disabled-reason.is-visible { display:block; }
  .advanced-title { font-family:Georgia,serif; font-size:20px; font-weight:normal; line-height:1.25; margin:22px 0 12px; color:var(--fg); }
  .advanced-subtitle { font-family:Georgia,serif; font-size:17px; font-weight:normal; line-height:1.25; margin:0 0 10px; color:var(--fg); }
  .advanced-section, .advanced-body, .advanced-subsection, .advanced-fields, .advanced-field { width:100%; box-sizing:border-box; padding-left:0; padding-right:0; }
  .advanced-section { border-top:1px solid var(--line); padding-top:14px; padding-bottom:0; margin:8px 0 10px; }
  .advanced-toggle { display:flex; align-items:center; gap:8px; margin:0 0 12px; color:var(--fg); font-size:12px; letter-spacing:0; text-transform:none; font-weight:650; }
  .advanced-section:not(.is-enabled) .advanced-toggle { margin-bottom:0; }
  .advanced-toggle input { flex:0 0 auto; }
  .advanced-body { margin:0; }
  .advanced-subsection { margin:0 0 18px; }
  .advanced-subsection + .advanced-subsection { padding-top:14px; border-top:1px solid var(--line); }
  .advanced-subsection:last-child { margin-bottom:0; }
  .advanced-fields { margin:0; }
  .advanced-field { margin:0 0 12px; padding-top:0; padding-bottom:0; }
  .advanced-fields > .advanced-field:last-child { margin-bottom:0; }
  .advanced-field input { margin-bottom:0; }
  .server-list-block { margin:0 0 16px; }
  .server-list-block + .server-list-block { padding-top:14px; border-top:1px solid var(--line); }
  .server-list-block:last-child { margin-bottom:0; }
  .server-list-title { font-family:Georgia,serif; font-size:15px; font-weight:normal; line-height:1.25; color:var(--fg); margin:0 0 10px; text-transform:none; letter-spacing:0; }
  .server-action-row, .server-chip-list { display:flex; align-items:flex-start; flex-wrap:wrap; gap:8px; margin:0 0 10px; }
  .server-action-chip, .server-chip { display:inline-flex; width:auto; max-width:100%; margin:0 !important; text-transform:none !important; letter-spacing:0 !important; line-height:1.25 !important; }
  .server-action-chip { align-items:center; gap:6px; padding:5px 9px !important; background:transparent !important; color:var(--fg) !important; border:1px solid var(--line) !important; font-size:11px !important; font-weight:650 !important; }
  .server-action-chip:disabled { color:var(--fg-3) !important; background:rgba(0,0,0,.03) !important; }
  .plus-icon { display:inline-flex; align-items:center; justify-content:center; width:14px; height:14px; border:1px solid currentColor; border-radius:50%; font-size:12px; line-height:1; font-weight:800; }
  .server-chip { position:relative; flex-direction:column; align-items:flex-start; gap:2px; min-width:0; padding:8px 24px 8px 10px !important; background:var(--pap) !important; color:var(--fg) !important; border:1px solid var(--line) !important; font-size:11px !important; font-weight:650 !important; }
  .server-chip.is-arateki { border-color:var(--pri) !important; box-shadow:inset 3px 0 0 var(--pri); }
  .server-chip-name { max-width:100%; padding-right:2px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
  .server-chip-url { max-width:210px; color:var(--fg-3); font-family:monospace; font-size:10px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
  .server-chip-remove { position:absolute; top:3px; right:4px; width:18px; height:18px; padding:0 !important; margin:0 !important; display:flex; align-items:center; justify-content:center; background:transparent !important; color:var(--fg-3) !important; border:none !important; font-size:14px !important; line-height:1 !important; }
  .server-input-row { display:grid; grid-template-columns:minmax(0,1fr) 38px; gap:8px; margin:0 0 10px; }
  .server-input-row.is-hidden { display:none; }
  .server-input-row input { margin:0; }
  .server-input-row button { margin:0; padding:8px !important; font-size:16px; line-height:1; }
  .identity-subsection .ident-section { margin:0; }
  .advanced-body.is-hidden { display:none; }
  form[action="/wifisave"] button[type="submit"] { margin-top:8px; }
  label { font-size:10px; color:var(--fg-3); margin-bottom:6px; display:block; text-transform:uppercase; letter-spacing:0.05em; font-weight:650; }
  .theme-btn { position:fixed; top:20px; right:max(20px, calc((100vw - 400px) / 2)); background:var(--bg) !important; border:none !important; color:var(--fg) !important; cursor:pointer; width:42px; height:42px; padding:0; margin:0; z-index:100; letter-spacing:0; text-transform:none; pointer-events:auto; display:flex; align-items:center; justify-content:center; }
  body > .wrap::before { content:''; display:block; position:fixed; top:66px; left:50%; transform:translateX(-50%); width:calc(100% - 40px); max-width:400px; height:1px; background:var(--line); pointer-events:none; z-index:91; }
  .section-sep { height:1px; background:var(--line); margin:20px 0; }
  .lang-sel { margin-bottom:20px; }
  dt { font-size:10px; text-transform:uppercase; letter-spacing:0.05em; color:var(--fg-3); margin-top:12px; font-weight:650; }
  dd { font-family:monospace; font-size:14px; margin-left:0; margin-bottom:8px; word-break:break-all; }
  #exit-confirm, #save-confirm { display:none; position:fixed; inset:0; z-index:10000; background:rgba(5,8,6,.55); align-items:center; justify-content:center; padding:20px; box-sizing:border-box; }
  #exit-confirm.is-open, #save-confirm.is-open { display:flex; }
  .exit-card { width:100%; max-width:360px; background:var(--bg); color:var(--fg); border:1px solid var(--line); padding:18px; border-radius:4px; box-shadow:0 8px 30px rgba(0,0,0,.28); }
  .exit-card p { margin:0 0 14px; color:var(--fg-2); font-size:13px; line-height:1.5; }
  .exit-actions { display:flex; gap:8px; }
  .exit-actions button { margin:0; padding:8px 12px; }
  .exit-cancel { background:transparent !important; color:var(--fg) !important; border-color:var(--line) !important; }
  .wm-credit { margin-top:24px; padding-top:14px; border-top:1px solid var(--line); display:flex; align-items:center; justify-content:center; gap:8px; color:var(--fg-3); font-size:10px; font-weight:650; letter-spacing:.04em; text-transform:uppercase; }
  .wm-credit svg { width:20px; height:20px; display:block; }
</style>
<script src="/portal.js"></script>
)rawliteral";

const char PORTAL_SCRIPT[] PROGMEM = R"rawliteral(
const dict = {
  '0': { conf:'Configure', info:'Info', exit:'Exit', upd:'Update', erase:'Erase WiFi config', setup:'Initial Setup', identity_title:'Identification', advanced_settings:'Advanced settings', connect_raiznet:'Connect to Raiznet servers', servers_section:'Servers', external_servers_title:'External server list', local_servers_title:'Local server list', add_arateki_server:'Add Arateki server', add_other_server:'Add other server', add_server:'Add server', server_url_placeholder:'Server URL', local_server_placeholder:'Server IP:Port', portal:'Configuration Portal', credit:'Developed by Arateki', lang:'Language', net:'-- Select Wi-Fi Network --', ssid:'Wi-Fi network name', other:'Other (Type Wi-Fi network name)', title_info:'Device Info', title_upd:'Update Firmware', noap:'No AP set', chip:'Chip ID', fsize:'Flash Size', exit_conf:'Close the configuration portal? You will need to restart the device to open it again.', exit_cancel:'Cancel', exit_confirm:'Close portal', exiting:'Closing...', exiting_msg:'The configuration portal is closing.', firmware:'Firmware', upload_fw:'Upload new firmware', update_hint:'May not function inside captive portal, open in browser http://192.168.4.1', save:'Save', back:'Back', refresh:'Refresh Wi-Fi List', password:'Password', showpass:'Show Password', wmode:'WiFi mode', mac:'MAC Address', stip:'Station IP', stmac:'Station MAC', bssid:'BSSID', apip:'Access point IP', apmac:'Access point MAC', ap_ssid:'Access point SSID', uptime:'Uptime', chip_rev:'Chip Rev', last_reset:'Last reset reason', psram:'PSRAM Size', cpu:'CPU Frequency', heap:'Free Heap', sketch:'Sketch Size', sdk:'SDK version', temp:'Temperature', wifi:'WiFi', conn:'Connected', autoconx:'Autoconnect', st_ssid:'Station SSID', st_gw:'Station Gateway', st_sub:'Station Subnet', dns:'DNS Server', host:'Hostname', ap_host:'Access point hostname', about:'About', wm:'WiFiManager', ard:'Arduino', build:'Build Date', master_key:'Your master key', create_tab:'Create', recover_tab:'Recover', recover_words:'Type the 12 words', recover_qr:'Upload QR code', recover_hint:'Use the words or a QR image saved from another setup.', qr_reading:'Reading QR code...', qr_loaded:'Master key loaded', qr_unsupported:'QR reading is not available in this browser. Type the words instead.', qr_not_found:'No QR code was found in this image.', save_disabled_reason:'Save is disabled because the imported words are not valid for the selected language.', copy_words:'Copy words', copied:'Copied', save_qr_btn:'Save QR code', reroll:'Change', backup_hint:'You can keep your key by copying the words or saving the QR code. Keep an offline copy in a safe place.', security_warn:'WARNING: These 12 words are your master key and work like a password. This key cannot be recovered after this setup is finished; a new one can only be generated optionally by resetting the device.', import_lbl:'Import Identity (Optional)', optional:'Optional', sensor_name:'Sensor Name', pub_server_name:'Public Server — Name', ext_server:'External Server (URL)', loc_server:'Local Server (IP:Port)', docs:'Docs' },
  '1': { conf:'Configurar', info:'Informações', exit:'Sair', upd:'Atualizar', erase:'Apagar Wi-Fi salvo', setup:'Configuração Inicial', identity_title:'Identificação', advanced_settings:'Configurações avançadas', connect_raiznet:'Conectar a servidores raiznet', servers_section:'Servidores', external_servers_title:'Lista de servidores externos', local_servers_title:'Lista de servidores locais', add_arateki_server:'Adicionar servidor Arateki', add_other_server:'Adicionar outro servidor', add_server:'Adicionar servidor', server_url_placeholder:'URL do servidor', local_server_placeholder:'IP:Porta do servidor', portal:'Portal de configuração', credit:'Desenvolvido por Arateki', lang:'Idioma / Language', net:'-- Selecionar Rede Wi-Fi --', ssid:'Nome da rede Wi-Fi', other:'Outra (Digitar nome da rede Wi-Fi)', title_info:'Informações', title_upd:'Atualizar Firmware', noap:'Nenhuma rede configurada', chip:'ID do Chip', fsize:'Tamanho da Flash', exit_conf:'Deseja fechar o portal de configuração? Você precisará reiniciar o dispositivo para abrir novamente', exit_cancel:'Cancelar', exit_confirm:'Fechar portal', exiting:'Encerrando...', exiting_msg:'O portal de configuração está sendo encerrado.', firmware:'Firmware', upload_fw:'Enviar novo firmware', update_hint:'Pode não funcionar dentro do portal cativo. Abra no navegador: http://192.168.4.1', save:'Salvar', back:'Voltar', refresh:'Atualizar Lista Wi-Fi', password:'Senha', showpass:'Mostrar senha', wmode:'Modo Wi-Fi', mac:'Endereço MAC', stip:'IP da Estação', stmac:'MAC da Estação', bssid:'BSSID', apip:'IP do AP', apmac:'MAC do AP', ap_ssid:'SSID do AP', uptime:'Tempo Ligado', chip_rev:'Revisão do Chip', last_reset:'Motivo do último reset', psram:'Tamanho da PSRAM', cpu:'Frequência CPU', heap:'Memória Livre', sketch:'Tamanho do Código', sdk:'Versão SDK', temp:'Temperatura', wifi:'Wi-Fi', conn:'Conectado', autoconx:'Conexão automática', st_ssid:'SSID da Estação', st_gw:'Gateway da Estação', st_sub:'Sub-rede da Estação', dns:'Servidor DNS', host:'Nome do Dispositivo', ap_host:'Nome do Portal', about:'Sobre', wm:'Versão WiFiManager', ard:'Versão Arduino', build:'Data de Build', master_key:'Sua chave-mestra', create_tab:'Criar', recover_tab:'Recuperar', recover_words:'Digite as 12 palavras', recover_qr:'Enviar QR code', recover_hint:'Use as palavras ou uma imagem QR salva de outra configuração.', qr_reading:'Lendo QR code...', qr_loaded:'Chave-mestra carregada', qr_unsupported:'Leitura de QR indisponível neste navegador. Digite as palavras.', qr_not_found:'Nenhum QR code foi encontrado nessa imagem.', save_disabled_reason:'O salvamento está desabilitado porque as palavras importadas não são válidas para o idioma selecionado.', copy_words:'Copiar palavras', copied:'Copiado', save_qr_btn:'Salvar QR code', reroll:'Trocar', backup_hint:'Você pode guardar sua chave copiando as palavras ou salvando o QR code. Guarde uma cópia offline em local seguro.', security_warn:'CUIDADO: Estas 12 palavras são sua chave-mestra e funcionam como uma senha. Não será possível recuperar essa chave após a finalização dessa configuração, sendo possível apenas gerar uma nova opcionalmente ao resetar o dispositivo.', import_lbl:'Importar Identidade (Opcional)', optional:'Opcional', sensor_name:'Nome do Sensor', pub_server_name:'Servidor Público — Nome', ext_server:'Servidor Externo (URL)', loc_server:'Servidor Local (IP:Porta)', docs:'Manual' },
  '2': { conf:'Configurar', info:'Información', exit:'Salir', upd:'Actualizar', erase:'Borrar Wi-Fi guardado', setup:'Configuración Inicial', identity_title:'Identificación', advanced_settings:'Configuración avanzada', connect_raiznet:'Conectar a servidores Raiznet', servers_section:'Servidores', external_servers_title:'Lista de servidores externos', local_servers_title:'Lista de servidores locales', add_arateki_server:'Añadir servidor Arateki', add_other_server:'Añadir otro servidor', add_server:'Añadir servidor', server_url_placeholder:'URL del servidor', local_server_placeholder:'IP:Puerto del servidor', portal:'Portal de configuración', credit:'Desarrollado por Arateki', lang:'Idioma / Language', net:'-- Seleccionar Red Wi-Fi --', ssid:'Nombre de red Wi-Fi', other:'Otra (Escribir nombre de red Wi-Fi)', title_info:'Información', title_upd:'Actualizar Firmware', noap:'Ninguna red configurada', chip:'ID del Chip', fsize:'Tamaño de Flash', exit_conf:'¿Desea cerrar el portal de configuración? Tendrá que reiniciar el dispositivo para abrirlo de nuevo.', exit_cancel:'Cancelar', exit_confirm:'Cerrar portal', exiting:'Cerrando...', exiting_msg:'El portal de configuración se está cerrando.', firmware:'Firmware', upload_fw:'Subir nuevo firmware', update_hint:'Puede no funcionar dentro del portal cautivo. Abra en el navegador: http://192.168.4.1', save:'Guardar', back:'Volver', refresh:'Actualizar lista Wi-Fi', password:'Contraseña', showpass:'Mostrar contraseña', wmode:'Modo Wi-Fi', mac:'Dirección MAC', stip:'IP de la Estación', stmac:'MAC de la Estación', bssid:'BSSID', apip:'IP del AP', apmac:'MAC del AP', ap_ssid:'SSID del AP', uptime:'Tiempo Encendido', chip_rev:'Revisión del Chip', last_reset:'Motivo del último reset', psram:'Tamaño de PSRAM', cpu:'Frecuencia CPU', heap:'Memoria Libre', sketch:'Tamaño del Código', sdk:'Versión SDK', temp:'Temperatura', wifi:'Wi-Fi', conn:'Conectado', autoconx:'Autoconexión', st_ssid:'SSID de Estación', st_gw:'Puerta de Enlace', st_sub:'Subred', dns:'Servidor DNS', host:'Nombre de Dispositivo', ap_host:'Nombre del Portal', about:'Acerca de', wm:'Versión WiFiManager', ard:'Versión Arduino', build:'Fecha de Build', master_key:'Su llave maestra', create_tab:'Crear', recover_tab:'Recuperar', recover_words:'Escriba las 12 palabras', recover_qr:'Subir QR', recover_hint:'Use las palabras o una imagen QR guardada de otra configuración.', qr_reading:'Leyendo QR...', qr_loaded:'Llave maestra cargada', qr_unsupported:'La lectura de QR no está disponible en este navegador. Escriba las palabras.', qr_not_found:'No se encontró ningún QR en esta imagen.', save_disabled_reason:'Guardar está deshabilitado porque las palabras importadas no son válidas para el idioma seleccionado.', copy_words:'Copiar palabras', copied:'Copiado', save_qr_btn:'Guardar QR', reroll:'Cambiar', backup_hint:'Puede guardar su llave copiando las palabras o guardando el código QR. Conserve una copia offline en un lugar seguro.', security_warn:'CUIDADO: Estas 12 palabras son su llave maestra y funcionan como una contraseña. No será posible recuperar esta llave después de finalizar esta configuración; solo podrá generar una nueva opcionalmente al resetear el dispositivo.', import_lbl:'Importar Identidad (Opcional)', optional:'Opcional', sensor_name:'Nombre del Sensor', pub_server_name:'Servidor Público — Nombre', ext_server:'Servidor Externo (URL)', loc_server:'Servidor Local (IP:Puerto)', docs:'Documentación' },
  '3': { conf:'設定', info:'情報', exit:'終了', upd:'更新', erase:'Wi-Fi設定を消去', setup:'初期設定', identity_title:'識別', advanced_settings:'詳細設定', connect_raiznet:'Raiznetサーバーに接続', servers_section:'サーバー', external_servers_title:'外部サーバー一覧', local_servers_title:'ローカルサーバー一覧', add_arateki_server:'Aratekiサーバーを追加', add_other_server:'別のサーバーを追加', add_server:'サーバーを追加', server_url_placeholder:'サーバーURL', local_server_placeholder:'サーバーIP:ポート', portal:'設定ポータル', credit:'Arateki 開発', lang:'言語', net:'-- Wi-Fiネットワーク選択 --', ssid:'Wi-Fiネットワーク名', other:'その他 (Wi-Fiネットワーク名入力)', title_info:'情報', title_upd:'ファームウェア更新', noap:'未設定', chip:'チップID', fsize:'フラッシュサイズ', exit_conf:'設定ポータルを閉じますか？再度開くにはデバイスを再起動する必要があります。', exit_cancel:'キャンセル', exit_confirm:'ポータルを閉じる', exiting:'終了しています...', exiting_msg:'設定ポータルを終了しています。', firmware:'ファームウェア', upload_fw:'新しいファームウェアをアップロード', update_hint:'キャプティブポータル内では動作しない場合があります。ブラウザで http://192.168.4.1 を開いてください', save:'保存', back:'戻る', refresh:'Wi-Fiリストを更新', password:'パスワード', showpass:'パスワードを表示', wmode:'WiFiモード', mac:'MACアドレス', stip:'ステーションIP', stmac:'ステーションMAC', bssid:'BSSID', apip:'AP IP', apmac:'AP MAC', ap_ssid:'AP SSID', uptime:'起動時間', chip_rev:'チップリビジョン', last_reset:'最終リセット理由', psram:'PSRAMサイズ', cpu:'CPU周波数', heap:'空きメモリ', sketch:'スケッチサイズ', sdk:'SDKバージョン', temp:'温度', wifi:'Wi-Fi', conn:'接続状態', autoconx:'自動接続', st_ssid:'ステーションSSID', st_gw:'ゲートウェイ', st_sub:'サブネット', dns:'DNS', host:'ホスト名', ap_host:'APホスト名', about:'情報', wm:'WiFiManagerバージョン', ard:'Arduinoバージョン', build:'ビルド日', master_key:'マスターキー', create_tab:'作成', recover_tab:'復元', recover_words:'12語を入力', recover_qr:'QRをアップロード', recover_hint:'単語または保存済みQR画像を使用します。', qr_reading:'QRを読み取り中...', qr_loaded:'マスターキーを読み込みました', qr_unsupported:'このブラウザではQRを読み取れません。単語を入力してください。', qr_not_found:'この画像にQRが見つかりません。', save_disabled_reason:'入力した単語が選択した言語で有効ではないため、保存できません。', copy_words:'単語をコピー', copied:'コピー済み', save_qr_btn:'QRを保存', reroll:'変更', backup_hint:'単語をコピーするかQRコードを保存してキーを保管できます。安全な場所にオフラインで保管してください。', security_warn:'注意: この12語はマスターキーであり、パスワードとして機能します。この設定の完了後はキーを復元できません。新しいキーは、必要に応じてデバイスをリセットした場合にのみ生成できます。', import_lbl:'アイデンティティをインポート (任意)', optional:'任意', sensor_name:'センサー名', pub_server_name:'公開サーバー — 名前', ext_server:'外部サーバー (URL)', loc_server:'ローカルサーバー (IP:ポート)', docs:'ドキュメント' },
  '4': { conf:'配置', info:'信息', exit:'退出', upd:'更新', erase:'清除 Wi-Fi 配置', setup:'初始设置', identity_title:'身份识别', advanced_settings:'高级设置', connect_raiznet:'连接到 Raiznet 服务器', servers_section:'服务器', external_servers_title:'外部服务器列表', local_servers_title:'本地服务器列表', add_arateki_server:'添加 Arateki 服务器', add_other_server:'添加其他服务器', add_server:'添加服务器', server_url_placeholder:'服务器 URL', local_server_placeholder:'服务器 IP:端口', portal:'配置门户', credit:'由 Arateki 开发', lang:'语言', net:'-- 选择 Wi-Fi 网络 --', ssid:'Wi-Fi 网络名称', other:'其他 (输入 Wi-Fi 网络名称)', title_info:'设备信息', title_upd:'更新固件', noap:'未设置网络', chip:'芯片ID', fsize:'闪存大小', exit_conf:'关闭配置门户？如需再次打开，您需要重启设备。', exit_cancel:'取消', exit_confirm:'关闭门户', exiting:'正在关闭...', exiting_msg:'配置门户正在关闭。', firmware:'固件', upload_fw:'上传新固件', update_hint:'在强制门户中可能无法工作。请在浏览器打开 http://192.168.4.1', save:'保存', back:'返回', refresh:'刷新 Wi-Fi 列表', password:'密码', showpass:'显示密码', wmode:'WiFi模式', mac:'MAC地址', stip:'站IP', stmac:'站MAC', bssid:'BSSID', apip:'AP IP', apmac:'AP MAC', ap_ssid:'AP SSID', uptime:'运行时间', chip_rev:'芯片版本', last_reset:'上次重置原因', psram:'PSRAM大小', cpu:'CPU频率', heap:'可用内存', sketch:'代码大小', sdk:'SDK版本', temp:'温度', wifi:'Wi-Fi', conn:'已连接', autoconx:'自动连接', st_ssid:'站 SSID', st_gw:'网关', st_sub:'子网掩码', dns:'DNS', host:'主机名', ap_host:'AP 主机名', about:'关于', wm:'WiFiManager版本', ard:'Arduino版本', build:'构建日期', master_key:'您的主密钥', create_tab:'创建', recover_tab:'恢复', recover_words:'输入12个助记词', recover_qr:'上传二维码', recover_hint:'使用助记词或其他设置保存的二维码图片。', qr_reading:'正在读取二维码...', qr_loaded:'主密钥已加载', qr_unsupported:'此浏览器不支持二维码读取。请手动输入助记词。', qr_not_found:'图片中未找到二维码。', save_disabled_reason:'导入的助记词与所选语言不匹配，因此无法保存。', copy_words:'复制助记词', copied:'已复制', save_qr_btn:'保存二维码', reroll:'更换', backup_hint:'您可以通过复制助记词或保存二维码来保管密钥。请离线保存在安全位置。', security_warn:'警告：这12个单词是您的主密钥，相当于密码。完成此配置后将无法恢复此密钥，只能在重置设备时选择生成新密钥。', import_lbl:'导入身份 (可选)', optional:'可选', sensor_name:'传感器名称', pub_server_name:'公共服务器 — 名称', ext_server:'外部服务器 (URL)', loc_server:'本地服务器 (IP:端口)', docs:'文档' }
};

const extraText = {
  '0': { cred_saved:'Credentials saved', settings_saved:'Settings saved', saving_credentials:'Saving credentials', trying_connect:'Trying to connect ESP to the network.', reconnect_retry:'If it fails, reconnect to the access point and try again.', not_connected:'Not connected', connected:'Connected', to:'to', with_ip:'with IP', ap_not_found:'AP not found', auth_failure:'Authentication failure', could_not_connect:'Could not connect' },
  '1': { cred_saved:'Credenciais salvas', settings_saved:'Configurações salvas', saving_credentials:'Salvando credenciais', trying_connect:'Tentando conectar o ESP à rede.', reconnect_retry:'Se falhar, reconecte-se ao ponto de acesso e tente novamente.', not_connected:'Não conectado', connected:'Conectado', to:'a', with_ip:'com IP', ap_not_found:'Rede não encontrada', auth_failure:'Falha de autenticação', could_not_connect:'Não foi possível conectar' },
  '2': { cred_saved:'Credenciales guardadas', settings_saved:'Configuración guardada', saving_credentials:'Guardando credenciales', trying_connect:'Intentando conectar el ESP a la red.', reconnect_retry:'Si falla, vuelva a conectarse al punto de acceso e inténtelo de nuevo.', not_connected:'No conectado', connected:'Conectado', to:'a', with_ip:'con IP', ap_not_found:'Red no encontrada', auth_failure:'Error de autenticación', could_not_connect:'No fue posible conectar' },
  '3': { cred_saved:'認証情報を保存しました', settings_saved:'設定を保存しました', saving_credentials:'認証情報を保存しています', trying_connect:'ESPをネットワークに接続しています。', reconnect_retry:'失敗した場合はアクセスポイントに再接続してもう一度お試しください。', not_connected:'未接続', connected:'接続済み', to:'to', with_ip:'IP', ap_not_found:'APが見つかりません', auth_failure:'認証に失敗しました', could_not_connect:'接続できませんでした' },
  '4': { cred_saved:'凭据已保存', settings_saved:'设置已保存', saving_credentials:'正在保存凭据', trying_connect:'正在将 ESP 连接到网络。', reconnect_retry:'如果失败，请重新连接到接入点后再试。', not_connected:'未连接', connected:'已连接', to:'到', with_ip:'IP', ap_not_found:'未找到 AP', auth_failure:'认证失败', could_not_connect:'无法连接' }
};

const textFor = (lang) => Object.assign({}, dict[lang] || dict['1'], extraText[lang] || extraText['1']);
const invalidMnemonicText = (lang) => ({'0':'Invalid words or language.','1':'Palavras fora do padrão ou idioma.','2':'Palabras fuera del patrón o idioma.','3':'単語または言語が違います。','4':'助记词或语言不匹配。'}[lang] || 'Palavras fora do padrão ou idioma.');
const missingWordsText = (lang, n) => ({
  '0':'Still missing ' + n + ' word' + (n === 1 ? '' : 's') + '.',
  '1':'Ainda faltam ' + n + ' palavra' + (n === 1 ? '' : 's') + '.',
  '2':'Todavía faltan ' + n + ' palabra' + (n === 1 ? '' : 's') + '.',
  '3':'あと' + n + '語必要です。',
  '4':'还缺少 ' + n + ' 个助记词。'
}[lang] || ('Ainda faltam ' + n + ' palavra' + (n === 1 ? '' : 's') + '.'));
const saveConfirmText = {
  '0': { msg:'Save this configuration? Your master key cannot be recovered after saving. Keep the words or QR code in a safe offline place before continuing.', cancel:'Review', ok:'Save' },
  '1': { msg:'Salvar esta configuração? A chave-mestra não poderá ser recuperada depois de salvar. Guarde as palavras ou o QR code em local seguro e offline antes de continuar.', cancel:'Revisar', ok:'Salvar' },
  '2': { msg:'¿Guardar esta configuración? La llave maestra no podrá recuperarse después de guardar. Conserve las palabras o el código QR en un lugar seguro y offline antes de continuar.', cancel:'Revisar', ok:'Guardar' },
  '3': { msg:'この設定を保存しますか？保存後はマスターキーを復元できません。続行する前に単語またはQRコードを安全な場所にオフラインで保管してください。', cancel:'確認する', ok:'保存' },
  '4': { msg:'保存此配置？保存后将无法恢复主密钥。继续前请将助记词或二维码离线保存在安全位置。', cancel:'检查', ok:'保存' }
};
const basicSaveConfirmText = {
  '0': { msg:'Save this configuration?', cancel:'Review', ok:'Save' },
  '1': { msg:'Salvar esta configuração?', cancel:'Revisar', ok:'Salvar' },
  '2': { msg:'¿Guardar esta configuración?', cancel:'Revisar', ok:'Guardar' },
  '3': { msg:'この設定を保存しますか？', cancel:'確認する', ok:'保存' },
  '4': { msg:'保存此配置？', cancel:'检查', ok:'保存' }
};
const saveTextFor = (lang, advanced) => (advanced ? (saveConfirmText[lang] || saveConfirmText['1']) : (basicSaveConfirmText[lang] || basicSaveConfirmText['1']));

const labelKeys = {
  'configure wifi':'conf', 'info':'info', 'exit':'exit', 'update':'upd', 'erase wifi config':'erase', 'save':'save', 'back':'back', 'refresh':'refresh',
  'captive portal':'portal', 'rede wi-fi cativa':'portal', 'portal de configuração':'portal', 'configuration portal':'portal',
  'credentials saved':'cred_saved', 'settings saved':'settings_saved', 'not connected':'not_connected',
  'ssid':'ssid', 'password':'password', 'show password':'showpass', 'no ap set':'noap',
  'chip id':'chip', 'flash size':'fsize', 'sdk version':'sdk', 'cpu frequency':'cpu',
  'wifi mode':'wmode', 'mac address':'mac', 'station ip':'stip', 'station mac':'stmac', 'bssid':'bssid',
  'access point ip':'apip', 'access point mac':'apmac', 'access point ssid':'ap_ssid', 'access point hostname':'ap_host',
  'uptime':'uptime', 'chip rev':'chip_rev', 'last reset reason':'last_reset', 'psram size':'psram',
  'memory - free heap':'heap', 'memory - sketch size':'sketch', 'temperature':'temp', 'connected':'conn', 'autoconnect':'autoconx',
  'station ssid':'st_ssid', 'station gateway':'st_gw', 'station subnet':'st_sub',
  'dns server':'dns', 'hostname':'host', 'wifi':'wifi', 'about':'about', 'wifimanager':'wm', 'arduino':'ard', 'build date':'build'
};

const readPref = (key, fallback) => {
  try {
    const stored = localStorage.getItem(key);
    if (stored) return stored;
  } catch (_) {}
  const match = document.cookie.match(new RegExp('(?:^|; )' + key + '=([^;]+)'));
  return match ? decodeURIComponent(match[1]) : fallback;
};
const writePref = (key, value) => {
  try { localStorage.setItem(key, value); } catch (_) {}
  document.cookie = key + '=' + encodeURIComponent(value) + ';path=/;max-age=31536000;SameSite=Lax';
};
const _sysTheme = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
document.documentElement.setAttribute('data-theme', readPref('theme', _sysTheme));

const _moonSvg = "<svg viewBox='0 0 24 24' width='20' height='20' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' aria-hidden='true'><path d='M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z'/></svg>";
const _sunSvg = "<svg viewBox='0 0 24 24' width='20' height='20' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' aria-hidden='true'><circle cx='12' cy='12' r='4'/><line x1='12' y1='2' x2='12' y2='6'/><line x1='12' y1='18' x2='12' y2='22'/><line x1='4.93' y1='4.93' x2='7.76' y2='7.76'/><line x1='16.24' y1='16.24' x2='19.07' y2='19.07'/><line x1='2' y1='12' x2='6' y2='12'/><line x1='18' y1='12' x2='22' y2='12'/><line x1='4.93' y1='19.07' x2='7.76' y2='16.24'/><line x1='16.24' y1='7.76' x2='19.07' y2='4.93'/></svg>";

document.addEventListener('DOMContentLoaded', () => {
  const loader = document.createElement('div');
  loader.id = 'loader-overlay';
  document.body.appendChild(loader);

  const exitModal = document.createElement('div');
  exitModal.id = 'exit-confirm';
  exitModal.innerHTML = "<div class='exit-card'><p id='exit-confirm-text'></p><div class='exit-actions'><button type='button' class='exit-cancel' id='exit-cancel'></button><button type='button' id='exit-ok'></button></div></div>";
  document.body.appendChild(exitModal);

  const saveModal = document.createElement('div');
  saveModal.id = 'save-confirm';
  saveModal.innerHTML = "<div class='exit-card'><p id='save-confirm-text'></p><div class='exit-actions'><button type='button' class='exit-cancel' id='save-cancel'></button><button type='button' id='save-ok'></button></div></div>";
  document.body.appendChild(saveModal);

  // Wrap content for max-width and center alignment
  const wrap = document.createElement('div');
  wrap.className = 'wrap';
  while (document.body.firstChild && document.body.firstChild !== loader && document.body.firstChild !== wrap) {
    wrap.appendChild(document.body.firstChild);
  }
  document.body.appendChild(wrap);

  // Remove stray <br> tags injected by WiFiManager between custom parameters
  Array.from(wrap.childNodes).forEach(n => { if (n.nodeName === 'BR') n.remove(); });

  // Align checkbox+label pairs via flex — skip text nodes (whitespace) and BRs to find the label
  wrap.querySelectorAll('input[type="checkbox"]').forEach(cb => {
    let sib = cb.nextSibling;
    while (sib && (sib.nodeName === 'BR' || sib.nodeType === 3)) {
      const nxt = sib.nextSibling;
      if (sib.nodeName === 'BR') sib.remove();
      sib = nxt;
    }
    if (!sib || (sib.tagName !== 'LABEL' && sib.tagName !== 'SPAN')) return;
    const d = document.createElement('div');
    d.style.cssText = 'display:flex;align-items:center;flex-wrap:nowrap;gap:8px;margin-bottom:16px;';
    cb.parentNode.insertBefore(d, cb);
    cb.style.cssText = 'width:18px;height:18px;min-width:18px;padding:0;margin:0;flex-shrink:0;appearance:auto;-webkit-appearance:checkbox;accent-color:var(--pri);';
    sib.style.cssText = 'margin:0;display:inline;text-transform:uppercase;font-size:10px;color:var(--fg-3);letter-spacing:0.05em;font-weight:650;';
    d.appendChild(cb);
    d.appendChild(sib);
  });

  const brandHtml = "<span class='eyebrow'>S A F R A S E N S E <span class='brand-aqua'>- A Q U A</span></span>";
  const topBrand = document.createElement('div');
  topBrand.className = 'portal-brand';
  topBrand.innerHTML = brandHtml;
  wrap.appendChild(topBrand);

  // Theme toggle
  const btn = document.createElement('button');
  btn.className = 'theme-btn';
  btn.type = 'button';
  btn.setAttribute('aria-label', 'Toggle theme');
  wrap.appendChild(btn);

  const doc = document.documentElement;
  const setT = (t) => { doc.setAttribute('data-theme', t); writePref('theme', t); btn.innerHTML = t === 'dark' ? _sunSvg : _moonSvg; };
  setT(readPref('theme', _sysTheme));
  btn.onclick = (e) => { e.preventDefault(); setT(doc.getAttribute('data-theme') === 'dark' ? 'light' : 'dark'); };

  const startLoading = () => document.body.classList.add('is-loading');
  const stopLoading = () => document.body.classList.remove('is-loading');
  const actionPath = (el) => {
    try { return new URL(el.getAttribute('action') || el.getAttribute('href') || '', location.href).pathname; }
    catch (_) { return ''; }
  };
  const isExitForm = (form) => form && actionPath(form) === '/exit';
  const isSaveForm = (form) => form && actionPath(form) === '/wifisave';
  const setSaveDisabled = (disabled, message, level) => {
    const lang = readPref('lang', '1');
    document.querySelectorAll('form').forEach(form => {
      if (!isSaveForm(form)) return;
      let reason = form.querySelector('.save-disabled-reason');
      const submit = form.querySelector('button:not([type="button"]), input[type="submit"]');
      if (!reason && submit) {
        reason = document.createElement('div');
        reason.className = 'save-disabled-reason';
        reason.setAttribute('data-i18n', 'save_disabled_reason');
        submit.parentNode.insertBefore(reason, submit);
      }
      if (reason) {
        reason.innerText = message || textFor(lang).save_disabled_reason || '';
        reason.classList.toggle('is-warn', level === 'warn');
        reason.classList.toggle('is-visible', !!disabled);
      }
      form.querySelectorAll('button, input[type="submit"], input[type="button"]').forEach(btn => {
        if (btn.type !== 'button') btn.disabled = !!disabled;
      });
    });
  };
  let pendingSaveForm = null;
  let saveConfirmed = false;
  let advancedEnabled = false;
  let validateIdentityImport = async () => true;
  const showExitConfirm = () => {
    const lang = readPref('lang', '1');
    const t = textFor(lang);
    document.getElementById('exit-confirm-text').innerText = t.exit_conf;
    document.getElementById('exit-cancel').innerText = t.exit_cancel;
    document.getElementById('exit-ok').innerText = t.exit_confirm;
    exitModal.classList.add('is-open');
  };
  const goExit = (e) => {
    if (e) {
      e.preventDefault();
      e.stopImmediatePropagation();
    }
    showExitConfirm();
    return false;
  };
  const showSaveConfirm = (form) => {
    const lang = readPref('lang', '1');
    const t = saveTextFor(lang, advancedEnabled);
    pendingSaveForm = form;
    document.getElementById('save-confirm-text').innerText = t.msg;
    document.getElementById('save-cancel').innerText = t.cancel;
    document.getElementById('save-ok').innerText = t.ok;
    saveModal.classList.add('is-open');
  };
  const goSave = async (e, form) => {
    if (e) {
      e.preventDefault();
      e.stopImmediatePropagation();
    }
    if (!(await validateIdentityImport())) return false;
    showSaveConfirm(form);
    return false;
  };
  document.getElementById('exit-cancel').onclick = () => exitModal.classList.remove('is-open');
  document.getElementById('exit-ok').onclick = () => {
    exitModal.classList.remove('is-open');
    startLoading();
    window.location.href = '/exit';
  };
  document.getElementById('save-cancel').onclick = () => {
    pendingSaveForm = null;
    saveModal.classList.remove('is-open');
  };
  document.getElementById('save-ok').onclick = () => {
    if (!pendingSaveForm) return;
    saveConfirmed = true;
    saveModal.classList.remove('is-open');
    startLoading();
    pendingSaveForm.submit();
  };

  document.addEventListener('click', (e) => {
    const link = e.target.closest('a[href]');
    if (link && actionPath(link) === '/exit') {
      goExit(e);
      return;
    }

    const trigger = e.target.closest('button, input[type="submit"], input[type="button"], .btn');
    if (trigger && trigger.tagName === 'LABEL') return;
    const form = trigger ? trigger.closest('form') : null;
    if (isExitForm(form)) {
      goExit(e);
    } else if (isSaveForm(form) && !saveConfirmed && trigger && trigger.type !== 'button') {
      goSave(e, form);
    }
  }, true);

  // Add loading animation to buttons, handling confirmation safely
  document.querySelectorAll('form').forEach(f => {
    f.addEventListener('submit', (e) => {
      if (isExitForm(f)) {
        goExit(e);
        return;
      }
      if (isSaveForm(f) && !saveConfirmed) {
        goSave(e, f);
        return;
      }
      startLoading();
    });
  });
  
  document.querySelectorAll('button:not(.theme-btn), .btn').forEach(b => {
    b.addEventListener('click', function(e) {
      if (this.tagName === 'LABEL') return;
      if (this.type !== 'button' && !this.closest('form[action="/exit"]')) startLoading();
    });
  });

  // Fix loading/theme state when navigating back from the browser history cache.
  window.addEventListener('pageshow', () => {
    stopLoading();
    setT(readPref('theme', _sysTheme));
  });
  window.addEventListener('focus', stopLoading);
  document.addEventListener('visibilitychange', () => {
    if (!document.hidden) stopLoading();
  });

  const setupIdentityBackupActions = () => {
    const tabs = document.querySelectorAll('.identity-tab');
    let resetImportPending = () => setSaveDisabled(false, '', '');
    let validateActiveRecover = () => {};
    tabs.forEach(tab => {
      tab.onclick = () => {
        const target = tab.getAttribute('data-tab');
        tabs.forEach(t => t.classList.toggle('is-active', t === tab));
        document.querySelectorAll('.identity-pane').forEach(p => p.classList.toggle('is-active', p.getAttribute('data-pane') === target));
        if (target === 'create') resetImportPending();
        else validateActiveRecover();
      };
    });

    const copyBtn = document.getElementById('copy-mnemonic');
    const saveBtn = document.getElementById('save-mnemonic-qr');
    const rerollBtn = document.getElementById('reroll-mnemonic');
    const mnemonicBox = document.getElementById('mnemonic-code');
    const qrCanvas = document.getElementById('mnemonic-qr');
    if (!copyBtn || !saveBtn || !rerollBtn || !mnemonicBox || !qrCanvas) return;

    const getMnemonic = () => mnemonicBox.getAttribute('data-mnemonic') || mnemonicBox.textContent.trim();
    const applyIdentityData = (data) => {
      mnemonicBox.textContent = data.mnemonic;
      mnemonicBox.setAttribute('data-mnemonic', data.mnemonic);
      qrCanvas.setAttribute('data-size', String(data.qrSize));
      qrCanvas.setAttribute('data-bits', data.qrBits);
      copyBtn.innerText = copyBtn.getAttribute('data-label');
      drawQr();
    };
    const fetchIdentityData = async (url) => {
      const res = await fetch(url, { cache: 'no-store' });
      if (!res.ok) throw new Error('identity fetch failed');
      applyIdentityData(await res.json());
    };
    const drawQr = () => {
      const size = parseInt(qrCanvas.getAttribute('data-size') || '0', 10);
      const bits = qrCanvas.getAttribute('data-bits') || '';
      if (!size || bits.length !== size * size) return;
      const pad = 2;
      const scale = Math.floor(qrCanvas.width / (size + pad * 2));
      const offset = Math.floor((qrCanvas.width - (size + pad * 2) * scale) / 2);
      const ctx = qrCanvas.getContext('2d');
      ctx.fillStyle = '#fff';
      ctx.fillRect(0, 0, qrCanvas.width, qrCanvas.height);
      ctx.fillStyle = '#111';
      for (let y = 0; y < size; y++) {
        for (let x = 0; x < size; x++) {
          if (bits[y * size + x] === '1') ctx.fillRect(offset + (x + pad) * scale, offset + (y + pad) * scale, scale, scale);
        }
      }
    };
    const copyFallback = (value) => {
      const area = document.createElement('textarea');
      area.value = value;
      area.setAttribute('readonly', '');
      area.style.position = 'fixed';
      area.style.opacity = '0';
      document.body.appendChild(area);
      area.select();
      try { document.execCommand('copy'); } catch (_) {}
      area.remove();
    };
    copyBtn.onclick = async () => {
      const value = getMnemonic();
      try {
        if (navigator.clipboard && window.isSecureContext) await navigator.clipboard.writeText(value);
        else copyFallback(value);
        copyBtn.innerText = copyBtn.getAttribute('data-done');
        setTimeout(() => copyBtn.innerText = copyBtn.getAttribute('data-label'), 1400);
      } catch (_) {
        copyFallback(value);
      }
    };
    saveBtn.onclick = () => {
      startLoading();
      qrCanvas.toBlob(async (blob) => {
        if (!blob) {
          stopLoading();
          return;
        }
        const file = new File([blob], 'safrasense-owner-key-qr.png', { type: 'image/png' });
        try {
          if (navigator.canShare && navigator.canShare({ files: [file] })) {
            await navigator.share({ files: [file], title: 'SafraSense QR code' });
          } else {
            const link = document.createElement('a');
            link.href = URL.createObjectURL(blob);
            link.download = file.name;
            document.body.appendChild(link);
            link.click();
            setTimeout(() => {
              URL.revokeObjectURL(link.href);
              link.remove();
            }, 0);
          }
        } finally {
          stopLoading();
        }
      }, 'image/png');
    };
    rerollBtn.onclick = async () => {
      startLoading();
      try {
        const lang = readPref('lang', '1');
        await fetchIdentityData('/identity/reroll?lang=' + encodeURIComponent(lang));
      } catch (_) {
      } finally {
        stopLoading();
      }
    };
    fetchIdentityData('/identity/current?lang=' + encodeURIComponent(langVal)).catch(() => drawQr());

      const importInput = document.getElementById('import_mnemonic');
      const importQr = document.getElementById('import-qr');
      const importStatus = document.getElementById('qr-import-status');
      const suggestionBar = document.getElementById('mnemonic-suggestions');
      if (importInput && importQr && importStatus) {
        let validateTimer = null;
        let importValid = true;
        const completeDeletionWords = {};
        const isRecoverActive = () => {
          const active = document.querySelector('.identity-tab.is-active');
          return active && active.getAttribute('data-tab') === 'recover';
        };
        const setImportValidity = (valid, message, level) => {
          importValid = valid;
          importInput.classList.toggle('is-error', level === 'error');
          setSaveDisabled(!valid, message, level);
        };
      const setImportStatus = (message, level) => {
        importStatus.innerText = message;
        importStatus.classList.toggle('is-error', level === 'error');
        importStatus.classList.toggle('is-warn', level === 'warn');
        importStatus.classList.toggle('is-success', level === 'success');
        setImportValidity(level !== 'error' && level !== 'warn', message, level);
      };
      const resizeImportInput = () => {
        importInput.style.height = 'auto';
        importInput.style.height = Math.max(importInput.scrollHeight, 84) + 'px';
      };
      const clearImportInput = () => {
        importInput.value = '';
        setSuggestions([]);
        resizeImportInput();
      };
      const currentWordPrefix = (text) => {
        const value = text.trim();
        if (!value) return '';
        const words = value.split(/\s+/);
        return words[words.length - 1] || '';
      };
      const replaceCurrentWord = (word) => {
        if (!word || /\s$/.test(importInput.value)) return false;
        const words = importInput.value.trim().split(/\s+/);
        if (!words.length) return false;
        words[words.length - 1] = word;
        completeDeletionWords[word.toLowerCase()] = true;
        importInput.value = words.join(' ') + ' ';
        importInput.setSelectionRange(importInput.value.length, importInput.value.length);
        resizeImportInput();
        return true;
      };
      const setSuggestions = (suggestions) => {
        if (!suggestionBar) return;
        suggestionBar.innerHTML = '';
        const available = suggestionBar.clientWidth || 0;
        let used = 0;
        const gap = 5;
        for (const word of (suggestions || []).slice(0, 6)) {
          const chip = document.createElement('button');
          chip.type = 'button';
          chip.className = 'word-suggestion';
          chip.innerText = word;
          chip.addEventListener('mousedown', (event) => event.preventDefault());
          chip.addEventListener('click', () => {
            if (replaceCurrentWord(word)) {
              setSuggestions([]);
              validateImportedWords(true);
            }
            importInput.focus();
          });
          suggestionBar.appendChild(chip);
          const width = chip.offsetWidth || 0;
          const nextUsed = used + (used ? gap : 0) + width;
          if (available && nextUsed > available) {
            suggestionBar.removeChild(chip);
            break;
          }
          used = nextUsed;
        }
      };
      const completeCurrentWord = (word, requestedPrefix) => {
        if (!word || /\s$/.test(importInput.value)) return false;
        const words = importInput.value.trim().split(/\s+/);
        if (!words.length) return false;
        const currentPrefix = words[words.length - 1].toLowerCase();
        const originalPrefix = requestedPrefix.toLowerCase();
        const suggestion = word.toLowerCase();
        if (!currentPrefix.startsWith(originalPrefix) || !suggestion.startsWith(currentPrefix)) return false;
        words[words.length - 1] = word;
        completeDeletionWords[suggestion] = true;
        importInput.value = words.join(' ') + ' ';
        setSuggestions([]);
        resizeImportInput();
        return true;
      };
      const validateImportedWords = async (showValid) => {
        const value = importInput.value.trim();
        const requestedPrefix = currentWordPrefix(value);
        const lang = readPref('lang', '1');
        if (!value) {
          setSuggestions([]);
          setImportStatus(textFor(lang).recover_hint, '');
          return true;
        }
        try {
          const res = await fetch('/identity/validate?lang=' + encodeURIComponent(lang), {
            method: 'POST',
            headers: { 'Content-Type': 'text/plain' },
            body: value
          });
          const data = await res.json();
          if (data && data.complete) {
            completeDeletionWords[requestedPrefix.toLowerCase()] = true;
            setSuggestions([]);
            if (showValid) {
              setImportStatus(textFor(lang).qr_loaded, 'success');
            } else {
              setImportValidity(true, '', '');
            }
            return true;
          } else if (data && data.partial) {
            if (data.suggestions && data.suggestions.length === 1 && completeCurrentWord(data.suggestions[0], requestedPrefix)) {
              return validateImportedWords(showValid);
            }
            if (!data.suggestions || data.suggestions.length === 0) {
              completeDeletionWords[requestedPrefix.toLowerCase()] = true;
            }
            setSuggestions(data.suggestions || []);
            const missing = data.missing || 0;
            const message = missingWordsText(lang, missing);
            setImportStatus(message, 'warn');
            return false;
          }
        } catch (_) {
        }
        setSuggestions([]);
        setImportStatus(invalidMnemonicText(lang), 'error');
        return false;
      };
      validateIdentityImport = async () => {
        if (!isRecoverActive()) return true;
        if (!importValid) return false;
        return validateImportedWords(false);
      };
      resetImportPending = () => {
        clearTimeout(validateTimer);
        importValid = true;
        importInput.classList.remove('is-error');
        importStatus.classList.remove('is-error', 'is-warn', 'is-success');
        importStatus.innerText = textFor(readPref('lang', '1')).recover_hint;
        setSuggestions([]);
        setSaveDisabled(false, '', '');
      };
      validateActiveRecover = () => {
        if (isRecoverActive()) validateImportedWords(true);
      };
      const scheduleImportValidation = () => {
        clearTimeout(validateTimer);
        validateTimer = setTimeout(() => validateImportedWords(true), 120);
      };
      const wordAtIndex = (value, index) => {
        if (index < 0 || index >= value.length || /\s/.test(value[index])) return '';
        let start = index;
        let end = index + 1;
        while (start > 0 && !/\s/.test(value[start - 1])) start--;
        while (end < value.length && !/\s/.test(value[end])) end++;
        return value.slice(start, end);
      };
      const deleteTargetsWord = (inputType) => {
        const start = importInput.selectionStart;
        const end = importInput.selectionEnd;
        const value = importInput.value;
        if (start === null || end === null || start !== end) return false;
        const word = inputType === 'deleteContentBackward' ? wordAtIndex(value, start - 1) :
          inputType === 'deleteContentForward' ? wordAtIndex(value, end) : '';
        return !!word && !!completeDeletionWords[word.toLowerCase()];
      };
      const removeWordAroundCaret = () => {
        const caret = importInput.selectionStart;
        const value = importInput.value;
        if (caret === null) return false;
        let start = caret;
        let end = caret;
        while (start > 0 && !/\s/.test(value[start - 1])) start--;
        while (end < value.length && !/\s/.test(value[end])) end++;
        if (start === end) return false;
        const before = value.slice(0, start).replace(/\s+$/, '');
        const after = value.slice(end).replace(/^\s+/, '');
        const join = before && after ? ' ' : '';
        importInput.value = before + join + after;
        const nextCaret = before.length + join.length;
        importInput.setSelectionRange(nextCaret, nextCaret);
        resizeImportInput();
        return true;
      };
      let deleteShouldRemoveWord = false;
      importInput.addEventListener('beforeinput', (event) => {
        deleteShouldRemoveWord = event.inputType && event.inputType.indexOf('deleteContent') === 0 && deleteTargetsWord(event.inputType);
      });
      importInput.addEventListener('keydown', (event) => {
        const inputType = event.key === 'Backspace' ? 'deleteContentBackward' : event.key === 'Delete' ? 'deleteContentForward' : '';
        if (!inputType || !deleteTargetsWord(inputType)) return;
        event.preventDefault();
        deleteShouldRemoveWord = false;
        removeWordAroundCaret();
        scheduleImportValidation();
      });
      importInput.addEventListener('input', (event) => {
        if (deleteShouldRemoveWord && event.inputType && event.inputType.indexOf('deleteContent') === 0) {
          removeWordAroundCaret();
        }
        deleteShouldRemoveWord = false;
        resizeImportInput();
        scheduleImportValidation();
      });
      importInput.addEventListener('blur', () => validateImportedWords(true));
      resizeImportInput();
      const loadImage = (file) => new Promise((resolve, reject) => {
        if ('createImageBitmap' in window) {
          createImageBitmap(file).then(resolve).catch(reject);
          return;
        }
        const img = new Image();
        img.onload = () => resolve(img);
        img.onerror = reject;
        img.src = URL.createObjectURL(file);
      });
      const imageSize = (image) => ({
        w: image.width || image.videoWidth || image.naturalWidth,
        h: image.height || image.videoHeight || image.naturalHeight
      });
      const bytesToBase64 = (bytes) => {
        let binary = '';
        const chunk = 4096;
        for (let i = 0; i < bytes.length; i += chunk) {
          binary += String.fromCharCode.apply(null, bytes.subarray(i, i + chunk));
        }
        return btoa(binary);
      };
      const packQrBitmap = (rgba, w, h) => {
        const packed = new Uint8Array(Math.ceil(w * h / 8));
        for (let i = 0, j = 0; i < rgba.length; i += 4, j++) {
          const gray = (rgba[i] * 30 + rgba[i + 1] * 59 + rgba[i + 2] * 11) / 100;
          if (gray < 128) packed[j >> 3] |= 1 << (7 - (j & 7));
        }
        return packed;
      };
      importQr.onchange = async () => {
        const file = importQr.files && importQr.files[0];
        const t = textFor(readPref('lang', '1'));
        if (!file) return;
        setImportStatus(t.qr_reading, '');
        try {
          const image = await loadImage(file);
          const size = imageSize(image);
          const maxSide = 240;
          const scale = Math.min(1, maxSide / Math.max(size.w, size.h));
          const w = Math.max(1, Math.round(size.w * scale));
          const h = Math.max(1, Math.round(size.h * scale));
          const canvas = document.createElement('canvas');
          canvas.width = w;
          canvas.height = h;
          const ctx = canvas.getContext('2d', { willReadFrequently: true });
          ctx.drawImage(image, 0, 0, w, h);
          const rgba = ctx.getImageData(0, 0, w, h).data;
          const res = await fetch('/identity/decode-qr?w=' + w + '&h=' + h, {
            method: 'POST',
            headers: { 'Content-Type': 'text/plain' },
            body: bytesToBase64(packQrBitmap(rgba, w, h))
          });
          const data = await res.json();
          const value = data && data.mnemonic ? data.mnemonic.trim() : '';
          if (value) {
            importInput.value = value;
            resizeImportInput();
            const valid = await validateImportedWords(true);
            if (!valid) {
              clearImportInput();
              setImportStatus(invalidMnemonicText(readPref('lang', '1')), 'error');
            }
          } else {
            clearImportInput();
            setImportStatus(t.qr_not_found, 'error');
          }
        } catch (_) {
          clearImportInput();
          setImportStatus(t.qr_not_found, 'error');
        }
      };
    }
  };
  const loadIdentitySection = async (lang) => {
    const root = document.getElementById('identity-root');
    if (!root) return;
    const res = await fetch('/identity/section?lang=' + encodeURIComponent(lang), { cache: 'no-store' });
    if (!res.ok) throw new Error('identity section fetch failed');
    root.innerHTML = await res.text();
  };
  const setIdentityTitle = (t) => {
    document.querySelectorAll('.ident-section').forEach(section => {
      const title = section.previousElementSibling;
      if (title && title.tagName === 'H1') title.innerText = t.identity_title || t.setup;
    });
  };

  const path = window.location.pathname;
  let langVal = readPref('lang', '1');
  if (!dict[langVal]) langVal = '1';

  const h1 = wrap.querySelector('h1');
  
  const walkText = (node, cb) => {
    if (node.nodeType === 3) cb(node);
    else node.childNodes.forEach(c => walkText(c, cb));
  };

  const translateStandardText = (t, lang) => {
    document.querySelectorAll('h1,h3,dt,label,button,strong').forEach(el => {
      const key = labelKeys[(el.textContent || '').trim().toLowerCase()];
      if (key && t[key]) el.innerText = t[key];
    });

    walkText(document.body, node => {
      node.nodeValue = node.nodeValue
        .replace('No AP set', t.noap)
        .replace('Saving Credentials', t.saving_credentials)
        .replace('Saving credentials', t.saving_credentials)
        .replace('Trying to connect ESP to network.', t.trying_connect)
        .replace('If it fails reconnect to AP to try again', t.reconnect_retry)
        .replace('AP not found', t.ap_not_found)
        .replace('Authentication failure', t.auth_failure)
        .replace('Could not connect', t.could_not_connect)
        .replace('with IP', t.with_ip)
        .replace(/^ to /, ' ' + t.to + ' ')
        .replace('Upload new firmware', t.upload_fw)
        .replace('* May not function inside captive portal, open in browser http://192.168.4.1', '* ' + t.update_hint)
        .replace('May not function inside captive portal, open in browser http://192.168.4.1', t.update_hint)
        .replace('Used / Total bytes', lang === '1' ? 'Usado / Total bytes' : (lang === '2' ? 'Usado / Total bytes' : 'Used / Total bytes'))
        .replace('bytes available', lang === '1' ? 'bytes disponíveis' : (lang === '2' ? 'bytes disponibles' : 'bytes available'))
        .replace('mins', lang === '1' ? 'min' : (lang === '2' ? 'min' : 'mins'))
        .replace('secs', lang === '1' ? 's' : (lang === '2' ? 's' : 'secs'))
        .replace(/^Yes$/, lang === '1' ? 'Sim' : (lang === '2' ? 'Sí' : 'Yes'))
        .replace(/^No$/, lang === '1' ? 'Não' : (lang === '2' ? 'No' : 'No'));
    });
  };

  const syncInternalLangControl = (lang) => {
    const langSelect = document.querySelector('select[name="lang"]');
    if (!langSelect || path === '/') return;
    langSelect.value = lang;
    langSelect.style.display = 'none';
    langSelect.setAttribute('aria-hidden', 'true');
    const label = langSelect.previousElementSibling;
    if (label && label.tagName === 'LABEL') label.style.display = 'none';
    langSelect.onchange = (e) => writePref('lang', e.target.value);
  };

  const removeInfoHelp = () => {
    if (path !== '/info') return;
    const help = Array.from(document.querySelectorAll('h3')).find(h => (h.textContent || '').trim().toLowerCase() === 'available pages');
    let node = help;
    while (node) {
      const next = node.nextSibling;
      node.remove();
      node = next;
    }
  };

  const removeDuplicatedIdentityHeader = () => {
    if (path !== '/wifi' && path !== '/0wifi') return;
    document.querySelectorAll('.ident-section').forEach(section => {
      const title = section.previousElementSibling;
      const brand = title ? title.previousElementSibling : null;
      if (brand && brand.classList && brand.classList.contains('eyebrow') && brand.textContent.includes('S A F R A')) {
        brand.remove();
      }
    });
  };

  const renderCredit = (t) => {
    let credit = document.getElementById('wm-credit');
    if (!credit) {
      credit = document.createElement('div');
      credit.id = 'wm-credit';
      credit.className = 'wm-credit';
      wrap.appendChild(credit);
    }
    credit.innerHTML = "<svg viewBox='0 0 400 400' aria-hidden='true'><g fill='currentColor'><path d='M 60,340 L 200,60 L 200,71.3 L 65.8,340 Z'/><path d='M 340,340 L 200,60 L 200,71.3 L 334.2,340 Z'/></g></svg><span>" + (t.credit || dict['0'].credit) + "</span><a href='/docs' style='margin-left:auto;color:var(--fg-3);font-size:10px;font-weight:650;letter-spacing:.04em;text-transform:uppercase;text-decoration:none;' onmouseover=\"this.style.color='var(--fg)'\" onmouseout=\"this.style.color='var(--fg-3)'\">" + (t.docs || 'Docs') + "</a>";
  };

  const applyTranslations = (lang) => {
    const t = textFor(lang);
    translateStandardText(t, lang);
    syncInternalLangControl(lang);
    removeInfoHelp();
    document.title = path === '/info' ? t.title_info : (path === '/update' ? t.title_upd : (path === '/wifisave' ? t.cred_saved : t.portal));

    if (h1) {
      if (h1.innerText.includes('WiFiManager') || h1.innerText.includes('Safra') || path === '/') {
        h1.innerText = t.setup;
      } else if (path === '/info') {
        h1.innerText = t.title_info;
      } else if (path === '/update') {
        h1.innerText = t.title_upd;
      } else if (path === '/wifi' || path === '/0wifi') {
        h1.innerText = t.setup;
      } else if (path === '/wifisave') {
        h1.innerText = t.cred_saved;
      }
    }
    setIdentityTitle(t);

    // Translate identity section elements that were rendered server-side
    const identRerollBtn = document.getElementById('reroll-mnemonic');
    if (identRerollBtn && t.reroll) identRerollBtn.innerText = t.reroll;
    const identCopyBtn = document.getElementById('copy-mnemonic');
    if (identCopyBtn && t.copy_words) {
      identCopyBtn.setAttribute('data-label', t.copy_words);
      identCopyBtn.setAttribute('data-done', t.copied || '');
      identCopyBtn.innerText = t.copy_words;
    }
    const identSaveQrBtn = document.getElementById('save-mnemonic-qr');
    if (identSaveQrBtn && t.save_qr_btn) identSaveQrBtn.innerText = t.save_qr_btn;
    const identMasterLabel = document.querySelector('.ident-section .eyebrow');
    if (identMasterLabel && t.master_key) identMasterLabel.innerText = t.master_key;
    const identBackupHint = document.querySelector('.backup-hint');
    if (identBackupHint && t.backup_hint) identBackupHint.innerText = t.backup_hint;
    const identWarnBox = document.querySelector('.warn-box');
    if (identWarnBox && t.security_warn) identWarnBox.innerText = t.security_warn;
    // Translate all elements with data-i18n attribute (server config labels)
    document.querySelectorAll('[data-i18n]').forEach(el => {
      const key = el.getAttribute('data-i18n');
      if (t[key]) el.innerText = t[key];
    });
    const serverFields = document.getElementById('server-fields');
    if (serverFields && serverFields._renderServerLists) serverFields._renderServerLists();

    if (path === '/') {
      document.querySelectorAll('form[action="/wifi"] button').forEach(b => b.innerText = t.conf);
      document.querySelectorAll('form[action="/info"] button').forEach(b => b.innerText = t.info);
      document.querySelectorAll('form[action="/update"] button').forEach(b => b.innerText = t.upd);
      document.querySelectorAll('form[action="/exit"] button').forEach(b => b.innerText = t.exit);
    } else if (path === '/info') {
      document.querySelectorAll('dt').forEach(dt => {
        if(dt.innerText.includes('Chip ID')) dt.innerText = t.chip;
        if(dt.innerText.includes('Flash')) dt.innerText = t.fsize;
        if(dt.innerText.includes('WiFi mode') || dt.innerText.includes('Modo Wi-Fi')) dt.innerText = t.wmode;
        if(dt.innerText.includes('Station IP') || dt.innerText.includes('IP da Estação')) dt.innerText = t.stip;
        if(dt.innerText.includes('Station MAC') || dt.innerText.includes('MAC da Estação')) dt.innerText = t.stmac;
        if(dt.innerText.includes('Soft AP IP') || dt.innerText.includes('Access point IP') || dt.innerText.includes('IP do AP')) dt.innerText = t.apip;
        if(dt.innerText.includes('Soft AP MAC') || dt.innerText.includes('Access point MAC') || dt.innerText.includes('MAC do AP')) dt.innerText = t.apmac;
        if(dt.innerText.includes('MAC Address')) dt.innerText = t.mac;
        if(dt.innerText.includes('BSSID')) dt.innerText = t.bssid;
      });
    } else if (path === '/update') {
      const ubtn = document.querySelector('input[type="submit"]');
      if (ubtn && ubtn.value === 'Update') ubtn.value = t.upd;
      const uploadBtn = document.getElementById('uploadbin');
      if (uploadBtn) uploadBtn.innerText = t.upd;
    } else if (path === '/exit') {
      wrap.innerHTML = '';
      wrap.appendChild(topBrand);
      wrap.appendChild(btn);
      const title = document.createElement('h1');
      title.innerText = t.exiting;
      const msg = document.createElement('div');
      msg.className = 'msg';
      msg.innerHTML = t.exiting_msg + "<br><br><div style='text-align:center;font-size:24px;animation:blink 1.4s infinite;color:var(--pri)'>•••</div>";
      wrap.appendChild(title);
      wrap.appendChild(msg);
    }
    renderCredit(t);
  };

  if (path === '/') {
    const langDiv = document.createElement('div');
    langDiv.className = 'lang-sel';
    langDiv.innerHTML = `<label id="lbl-lang">${dict[langVal].lang}</label>
      <select id="root-lang">
        <option value="1" ${langVal==='1'?'selected':''}>Português</option>
        <option value="0" ${langVal==='0'?'selected':''}>English</option>
        <option value="2" ${langVal==='2'?'selected':''}>Español</option>
        <option value="3" ${langVal==='3'?'selected':''}>日本語</option>
        <option value="4" ${langVal==='4'?'selected':''}>简体中文</option>
      </select>`;
    if(h1) h1.after(langDiv);
    
    document.getElementById('root-lang').onchange = (e) => {
      const newLang = e.target.value;
      writePref('lang', newLang);
      document.getElementById('lbl-lang').innerText = dict[newLang].lang;
      applyTranslations(newLang);
    };
  }

  const setAdvancedFieldsDisabled = (disabled) => {
    ['ext_servers', 'loc_servers'].forEach(name => {
      const input = document.querySelector('[name="' + name + '"]');
      if (input) input.disabled = !!disabled;
    });
  };

  const clearAdvancedIdentity = () => {
    const root = document.getElementById('identity-root');
    if (root) root.innerHTML = '';
    validateIdentityImport = async () => true;
    setSaveDisabled(false, '', '');
  };

  const setupServerLists = () => {
    const root = document.getElementById('server-fields');
    const extInput = document.getElementById('ext_servers');
    const locInput = document.getElementById('loc_servers');
    if (!root || !extInput || !locInput) return;
    if (root.dataset.ready === '1') {
      if (root._renderServerLists) root._renderServerLists();
      return;
    }
    root.dataset.ready = '1';
    const aratekiName = root.getAttribute('data-arateki-name') || 'Arateki';
    const aratekiUrl = root.getAttribute('data-arateki-url') || '';

    const parse = (input) => {
      try {
        const value = JSON.parse(input.value || '[]');
        return Array.isArray(value) ? value.filter(item => item && item.url).map(item => ({
          name: String(item.name || '').trim(),
          url: String(item.url || '').trim()
        })).filter(item => item.url).slice(0, 16) : [];
      } catch (_) {
        return [];
      }
    };
    const ext = parse(extInput);
    const loc = parse(locInput);
    const syncHidden = () => {
      extInput.value = JSON.stringify(ext);
      locInput.value = JSON.stringify(loc);
    };
    const tNow = () => textFor(readPref('lang', '1'));
    const plusChip = (label, disabled, onClick) => {
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'server-action-chip';
      btn.disabled = !!disabled;
      btn.innerHTML = "<span class='plus-icon'>+</span><span></span>";
      btn.querySelector('span:last-child').innerText = label;
      btn.onclick = onClick;
      return btn;
    };
    const makeBlock = (titleKey, placeholderKey) => {
      const block = document.createElement('div');
      block.className = 'server-list-block';
      const title = document.createElement('h4');
      title.className = 'server-list-title';
      title.setAttribute('data-i18n', titleKey);
      const actions = document.createElement('div');
      actions.className = 'server-action-row';
      const chips = document.createElement('div');
      chips.className = 'server-chip-list';
      const row = document.createElement('div');
      row.className = 'server-input-row is-hidden';
      const input = document.createElement('input');
      input.type = 'text';
      input.autocomplete = 'off';
      const addBtn = document.createElement('button');
      addBtn.type = 'button';
      addBtn.innerText = '+';
      row.appendChild(input);
      row.appendChild(addBtn);
      block.appendChild(title);
      block.appendChild(actions);
      block.appendChild(chips);
      block.appendChild(row);
      root.appendChild(block);
      return { title, actions, chips, row, input, addBtn, titleKey, placeholderKey };
    };
    const extBlock = makeBlock('external_servers_title', 'server_url_placeholder');
    const locBlock = makeBlock('local_servers_title', 'local_server_placeholder');
    const showInput = (block) => {
      block.row.classList.remove('is-hidden');
      block.input.focus();
    };
    const hideInput = (block) => {
      block.input.value = '';
      block.row.classList.add('is-hidden');
    };
    const addManual = (list, block, name) => {
      const url = block.input.value.trim();
      if (!url) return;
      if (!list.some(item => item.url === url)) list.push({ name, url });
      hideInput(block);
      render();
    };
    const renderChips = (list, block) => {
      block.chips.innerHTML = '';
      list.forEach((server, index) => {
        const chip = document.createElement('div');
        chip.className = 'server-chip' + (server.url === aratekiUrl ? ' is-arateki' : '');
        const name = document.createElement('span');
        name.className = 'server-chip-name';
        name.innerText = server.name || server.url;
        const url = document.createElement('span');
        url.className = 'server-chip-url';
        url.innerText = server.url;
        const remove = document.createElement('button');
        remove.type = 'button';
        remove.className = 'server-chip-remove';
        remove.innerText = '×';
        remove.onclick = () => {
          list.splice(index, 1);
          render();
        };
        chip.appendChild(name);
        chip.appendChild(url);
        chip.appendChild(remove);
        block.chips.appendChild(chip);
      });
    };
    var render = () => {
      const t = tNow();
      extBlock.title.innerText = t.external_servers_title;
      locBlock.title.innerText = t.local_servers_title;
      extBlock.input.placeholder = t.server_url_placeholder;
      locBlock.input.placeholder = t.local_server_placeholder;
      extBlock.actions.innerHTML = '';
      locBlock.actions.innerHTML = '';
      const hasArateki = ext.some(item => item.url === aratekiUrl || item.name === aratekiName);
      extBlock.actions.appendChild(plusChip(t.add_arateki_server, hasArateki, () => {
        if (!hasArateki && aratekiUrl) {
          ext.push({ name: aratekiName, url: aratekiUrl });
          render();
        }
      }));
      extBlock.actions.appendChild(plusChip(t.add_other_server, false, () => showInput(extBlock)));
      locBlock.actions.appendChild(plusChip(t.add_server, false, () => showInput(locBlock)));
      extBlock.addBtn.onclick = () => addManual(ext, extBlock, 'External');
      locBlock.addBtn.onclick = () => addManual(loc, locBlock, 'Local');
      extBlock.input.onkeydown = (event) => {
        if (event.key === 'Enter') {
          event.preventDefault();
          addManual(ext, extBlock, 'External');
        }
      };
      locBlock.input.onkeydown = (event) => {
        if (event.key === 'Enter') {
          event.preventDefault();
          addManual(loc, locBlock, 'Local');
        }
      };
      renderChips(ext, extBlock);
      renderChips(loc, locBlock);
      syncHidden();
    };
    root._renderServerLists = render;
    render();
  };

  const setupAdvancedSection = async (lang) => {
    const toggle = document.getElementById('connect-raiznet');
    const body = document.getElementById('advanced-body');
    if (!toggle || !body) return;
    setupServerLists();

    const sync = async () => {
      advancedEnabled = !!toggle.checked;
      const section = toggle.closest('.advanced-section');
      if (section) section.classList.toggle('is-enabled', advancedEnabled);
      body.classList.toggle('is-hidden', !advancedEnabled);
      setAdvancedFieldsDisabled(!advancedEnabled);
      if (!advancedEnabled) {
        clearAdvancedIdentity();
        return;
      }
      await loadIdentitySection(readPref('lang', lang));
      applyTranslations(readPref('lang', lang));
      setupIdentityBackupActions();
    };

    toggle.addEventListener('change', () => { sync().catch(() => {}); });
    await sync();
  };
  
  setupAdvancedSection(langVal).catch(() => {}).finally(() => {
    applyTranslations(langVal);
  });

  // Custom Wi-Fi Select on /wifi
  if (path === '/wifi') {
    const networks = [];
    wrap.querySelectorAll('a[href^="#p"]').forEach(a => {
      const rawSsid = a.getAttribute('data-ssid');
      const ssid = rawSsid !== null ? rawSsid : (a.textContent || '').replace(/\u00a0/g, ' ').trim();
      if (ssid) networks.push(ssid);
      const row = a.closest('div');
      if (row) row.style.display = 'none';
      else a.style.display = 'none';
    });
    wrap.querySelectorAll('.q').forEach(q => q.style.display = 'none');
    
    const ssidInput = document.getElementById('s');
    if (ssidInput && networks.length > 0) {
      const sel = document.createElement('select');
      const addOption = (value, label) => {
        const opt = document.createElement('option');
        opt.value = value;
        opt.textContent = label;
        sel.appendChild(opt);
      };
      addOption('', dict[langVal].net);
      networks.forEach(n => addOption(n, n));
      addOption('_other_', dict[langVal].other);
      
      ssidInput.parentNode.insertBefore(sel, ssidInput);
      
      const updateVis = () => {
        if (sel.value === '_other_') {
          ssidInput.style.display = 'block';
          ssidInput.value = '';
        } else {
          ssidInput.style.display = 'none';
          ssidInput.value = sel.value;
        }
      };
      sel.onchange = updateVis;
      updateVis();
    }
  }

  const langSelect = document.querySelector('select[name="lang"]');
  if (langSelect && path !== '/') {
    langSelect.value = langVal;
    langSelect.onchange = (e) => writePref('lang', e.target.value);
  }
});
)rawliteral";

void setupWifi(DeviceConfig& cfg) {
#ifdef WOKWI_EMULATOR
  // Emulador: sem portal captivo. Conecta direto na rede virtual do Wokwi.
  WiFi.mode(WIFI_STA);
  WiFi.begin(WOKWI_WIFI_SSID, "", 6);  // canal 6 acelera o associate no Wokwi
  Serial.print("[wifi] Conectando na Wokwi-GUEST");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print('.');
  }
  Serial.printf("\n[wifi] Conectado, IP %s\n", WiFi.localIP().toString().c_str());

  String emuMac = WiFi.macAddress();
  emuMac.replace(":", "");
  String emuSuffix = emuMac.substring(emuMac.length() - 4);
  emuSuffix.toLowerCase();
  mdnsName = "safrasense-aqua-" + emuSuffix;

  configTime(NTP_GMT_OFFSET_SEC, NTP_DAYLIGHT_SEC, NTP_SERVER_1, NTP_SERVER_2);
  MDNS.begin(mdnsName.c_str());
  return;
#endif

  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_MINUS_1dBm);

  DeviceIdentity id = loadOrCreateIdentity();
  // Owner identity shown in the captive portal is a draft until the user saves.
  // Keep it empty until the portal page requests it, after the AP/RF is active.
  id.lang = LANG_PT;
  id.mnemonic = "";

  String mac = WiFi.macAddress();
  mac.replace(":", "");
  String suffix = mac.substring(mac.length() - 4);
  suffix.toLowerCase();

  mdnsName      = "safrasense-aqua-" + suffix;
  String apName = "Safrasense-aqua_" + suffix;

  WiFiManager wm;
  wm.setTitle("Portal de configuração");
  wm.setConfigPortalTimeout(600); // 10 minutes to give enough time to write down the words
  
  // Apply our custom CSS and JS to ALL pages (menu, wifi list, setup form, info)
  wm.setCustomHeadElement(IDENTITY_CSS);

  // Keep the WiFiManager parameter small. The identity UI is loaded after the
  // page renders because large custom HTML blocks make getParamOut() fragile.
  String extServersJson = htmlEscape(serverListJson(cfg.servers_external));
  String locServersJson = htmlEscape(serverListJson(cfg.servers_local));

  String headerHtml =
    "<h1>" + t("setup_title", id.lang) + "</h1>";
  WiFiManagerParameter p_header(headerHtml.c_str());

  String advancedHtml =
    "<h2 class='advanced-title' data-i18n='advanced_settings'>" + t("advanced_settings", id.lang) + "</h2>"
    "<div class='advanced-section'>"
      "<label class='advanced-toggle'>"
        "<input type='checkbox' id='connect-raiznet' name='connect_raiznet' value='1'>"
        "<span data-i18n='connect_raiznet'>" + t("connect_raiznet", id.lang) + "</span>"
      "</label>"
      "<div id='advanced-body' class='advanced-body is-hidden'>"
        "<div class='advanced-subsection server-subsection'>"
          "<h3 class='advanced-subtitle' data-i18n='servers_section'>" + t("servers_section", id.lang) + "</h3>"
          "<div id='server-fields' class='advanced-fields'></div>"
        "</div>"
        "<div class='advanced-subsection identity-subsection'>"
          "<h3 class='advanced-subtitle' data-i18n='identity_title'>" + t("identity_title", id.lang) + "</h3>"
          "<div id='identity-root'></div>"
        "</div>"
      "</div>"
    "</div>";
  advancedHtml.replace("<div id='server-fields' class='advanced-fields'></div>",
    "<div id='server-fields' class='advanced-fields' data-arateki-name='" + htmlEscape(String(DEFAULT_SERVER_EXT_NAME)) + "' data-arateki-url='" + htmlEscape(String(DEFAULT_SERVER_EXT_URL)) + "'>"
      "<input type='hidden' id='ext_servers' name='ext_servers' value='" + extServersJson + "'>"
      "<input type='hidden' id='loc_servers' name='loc_servers' value='" + locServersJson + "'>"
    "</div>");
  WiFiManagerParameter p_advanced(advancedHtml.c_str());
  
  const char* langHtml = "<label>Idioma / Language</label>"
                         "<select name='lang'>"
                         "<option value='1' selected>Português</option>"
                         "<option value='0'>English</option>"
                         "<option value='2'>Español</option>"
                         "<option value='3'>日本語</option>"
                         "<option value='4'>简体中文</option></select>";
  WiFiManagerParameter p_lang_html(langHtml);

  String nameLabel = "<label data-i18n='sensor_name'>" + t("sensor_name", id.lang) + "</label>";
  WiFiManagerParameter p_name_label(nameLabel.c_str());
  WiFiManagerParameter p_name("name", "", cfg.device_name.c_str(), 32);

  wm.addParameter(&p_header);
  wm.addParameter(&p_lang_html);
  
  wm.addParameter(&p_name_label);
  wm.addParameter(&p_name);
  wm.addParameter(&p_advanced);

  wm.setAPCallback([](WiFiManager*) {
    setLedState(LED_PORTAL_OPEN);
  });

  wm.setWebServerCallback([&wm, &id]() {
    wm.server->on("/portal.js", HTTP_GET, [&wm]() {
      wm.server->send_P(200, PSTR("application/javascript"), PORTAL_SCRIPT, strlen_P(PORTAL_SCRIPT));
    });
    wm.server->on("/identity/section", HTTP_GET, [&wm, &id]() {
      Language lang = id.lang;
      if (wm.server->hasArg("lang")) {
        lang = docToLang(wm.server->arg("lang"));
      }
      if (id.mnemonic.length() == 0 || lang != id.lang) {
        generateOwnerIdentity(id, lang);
      }
      wm.server->send(200, "text/html", renderIdentitySectionHtml(id));
    });
    wm.server->on("/identity/current", HTTP_GET, [&wm, &id]() {
      Language lang = id.lang;
      if (wm.server->hasArg("lang")) {
        lang = docToLang(wm.server->arg("lang"));
      }
      if (id.mnemonic.length() == 0 || lang != id.lang) {
        generateOwnerIdentity(id, lang);
      }
      uint8_t newQrSize = 0;
      String newQrBits = renderMnemonicQrBits(id.mnemonic, newQrSize);
      String body = "{\"mnemonic\":\"" + jsonEscape(id.mnemonic) + "\",\"qrSize\":" + String(newQrSize) + ",\"qrBits\":\"" + newQrBits + "\"}";
      wm.server->send(200, "application/json", body);
    });
    wm.server->on("/identity/reroll", HTTP_GET, [&wm, &id]() {
      Language lang = docToLang(wm.server->arg("lang"));
      generateOwnerIdentity(id, lang);
      uint8_t newQrSize = 0;
      String newQrBits = renderMnemonicQrBits(id.mnemonic, newQrSize);
      String body = "{\"mnemonic\":\"" + jsonEscape(id.mnemonic) + "\",\"qrSize\":" + String(newQrSize) + ",\"qrBits\":\"" + newQrBits + "\"}";
      wm.server->send(200, "application/json", body);
    });
    wm.server->on("/identity/validate", HTTP_POST, [&wm]() {
      const Language lang = docToLang(wm.server->arg("lang"));
      const String body = wm.server->arg("plain");
      const MnemonicValidationResult result = analyzeMnemonicForLanguage(body, lang);
      String suggestionsJson = "[";
      const int suggestionsToSend = result.suggestionCount > 6 ? 6 : result.suggestionCount;
      for (int i = 0; i < suggestionsToSend; i++) {
        if (i > 0) suggestionsJson += ",";
        suggestionsJson += "\"" + jsonEscape(result.suggestions[i]) + "\"";
      }
      suggestionsJson += "]";
      if (result.complete) {
        wm.server->send(200, "application/json", "{\"ok\":true,\"complete\":true,\"partial\":false,\"words\":12,\"missing\":0,\"suggestions\":[]}");
      } else if (result.partial) {
        wm.server->send(200, "application/json", "{\"ok\":false,\"complete\":false,\"partial\":true,\"words\":" + String(result.wordCount) + ",\"missing\":" + String(result.missingWords) + ",\"suggestionTotal\":" + String(result.suggestionCount) + ",\"suggestions\":" + suggestionsJson + "}");
      } else {
        wm.server->send(200, "application/json", "{\"ok\":false,\"complete\":false,\"partial\":false,\"error\":\"" + jsonEscape(result.error) + "\",\"suggestions\":[]}");
      }
    });
    wm.server->on("/docs", HTTP_GET, [&wm]() {
      wm.server->send(200, "text/html", buildDocsPortalPage());
    });
    wm.server->on("/identity/decode-qr", HTTP_POST, [&wm]() {
      const int width = wm.server->arg("w").toInt();
      const int height = wm.server->arg("h").toInt();
      const String body = wm.server->arg("plain");
      LOG_DEBUGF("qr", "request width=%d height=%d b64_len=%u heap=%u\n", width, height, (unsigned)body.length(), (unsigned)ESP.getFreeHeap());
      String payload;
      String error;
      if (decodeQrBase64PackedBitmap(body, width, height, payload, error)) {
        LOG_INFOF("qr", "response ok payload_len=%u heap=%u\n", (unsigned)payload.length(), (unsigned)ESP.getFreeHeap());
        wm.server->send(200, "application/json", "{\"mnemonic\":\"" + jsonEscape(payload) + "\"}");
      } else {
        LOG_INFOF("qr", "response fail error=%s heap=%u\n", error.length() ? error.c_str() : "qr_not_found", (unsigned)ESP.getFreeHeap());
        wm.server->send(400, "application/json", "{\"error\":\"" + jsonEscape(error.length() ? error : String("qr_not_found")) + "\"}");
      }
    });
  });

  // Let's use the non-blocking approach to keep the main loop running.
  wm.setConfigPortalBlocking(false);

  if (!wm.autoConnect(apName.c_str())) {
    // If it fails to connect, it will start the portal in non-blocking mode.
    // We need a loop here to keep it alive while portal is active.
    while (wm.getConfigPortalActive()) {
      wm.process();
      tickLeds();
      delay(10);
    }
  }

  // ── Processing ──────────────────────────────────────────────────────────
  
  // Only process parameters if we actually ran the config portal.
  if (wm.server && wm.server->args() > 0) {
    // 1. Language
    id.lang = docToLang(wm.server->arg("lang"));
    
    const bool connectRaiznet = wm.server->hasArg("connect_raiznet");

    // 2. Mnemonic (only applies when Raiznet server connectivity is enabled).
    if (connectRaiznet) {
      String imported = wm.server->arg("import_mnemonic");
      imported.trim();
      if (imported.length() > 0) {
        String importError;
        if (validateMnemonicForLanguage(imported, id.lang, importError)) {
          importOwnerIdentity(id, imported);
        }
      } else if (id.mnemonic.length() == 0) {
        generateOwnerIdentity(id, id.lang);
      }
      saveIdentity(id);
    }

    // 3. Settings
    cfg.device_name = String(p_name.getValue());
    cfg.servers_external.clear();
    cfg.servers_local.clear();

    if (connectRaiznet) {
      parseServerListJson(wm.server->arg("ext_servers"), cfg.servers_external, "External");
      parseServerListJson(wm.server->arg("loc_servers"), cfg.servers_local, "Local");
    }

    saveConfig(cfg);
  }
  
  configTime(NTP_GMT_OFFSET_SEC, NTP_DAYLIGHT_SEC, NTP_SERVER_1, NTP_SERVER_2);
  MDNS.begin(mdnsName.c_str());
}

void reconnectWifi() {
  WiFi.disconnect(false);
  delay(500);
  WiFi.begin();
  configTime(NTP_GMT_OFFSET_SEC, NTP_DAYLIGHT_SEC, NTP_SERVER_1, NTP_SERVER_2);
  MDNS.begin(mdnsName.c_str());
}

String getMdnsName() { return mdnsName; }
