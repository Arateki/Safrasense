#include "http_local.h"
#include "config.h"
#include "telemetry/telemetry.h"
#include "telemetry/buffer.h"
#include "identity/identity.h"
#include "storage/storage.h"
#include "wifi_setup/wifi_setup.h"
#include "docs/docs.h"
#include "i18n/i18n.h"
#include <WebServer.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <pgmspace.h>
#include <string.h>

static WebServer      server(80);
static DeviceConfig*       gCfg = nullptr;
static const DeviceIdentity* gId  = nullptr;
static SensorData     gLastReading;
static bool           gHasReading   = false;
static PendingAction  gPendingAction = ACTION_NONE;

static const char* LOCAL_HEADER_KEYS[] = { "Accept-Language" };

static String normalizeLocalLangCode(String code) {
  code.trim();
  code.toLowerCase();
  if (code == "pt" || code.startsWith("pt-") || code == "1") return "1";
  if (code == "en" || code.startsWith("en-") || code == "0") return "0";
  if (code == "es" || code.startsWith("es-") || code == "2") return "2";
  if (code == "ja" || code.startsWith("ja-") || code == "3") return "3";
  if (code == "zh" || code.startsWith("zh-") || code == "4") return "4";
  return "0";
}

static String systemLocalLangCode() {
  String header = server.header("Accept-Language");
  header.toLowerCase();
  int start = 0;
  while (start < header.length()) {
    int end = header.indexOf(',', start);
    if (end < 0) end = header.length();
    String item = header.substring(start, end);
    int params = item.indexOf(';');
    if (params >= 0) item = item.substring(0, params);
    item.trim();
    if (item == "pt" || item.startsWith("pt-")) return "1";
    if (item == "en" || item.startsWith("en-")) return "0";
    if (item == "es" || item.startsWith("es-")) return "2";
    if (item == "ja" || item.startsWith("ja-")) return "3";
    if (item == "zh" || item.startsWith("zh-")) return "4";
    start = end + 1;
  }
  return "0";
}

static String currentLocalLangCode() {
  String code = server.arg("lang");
  if (code.length() > 0) return normalizeLocalLangCode(code);
  return systemLocalLangCode();
}

static Language currentLocalLanguage() {
  String code = currentLocalLangCode();
  if (code == "1") return LANG_PT;
  if (code == "2") return LANG_ES;
  if (code == "3") return LANG_JA;
  if (code == "4") return LANG_ZH;
  return LANG_EN;
}

static const char* localHtmlLang(Language lang) {
  if (lang == LANG_PT) return "pt-BR";
  if (lang == LANG_ES) return "es";
  if (lang == LANG_JA) return "ja";
  if (lang == LANG_ZH) return "zh-CN";
  return "en";
}

static const char* localChoice(Language lang, const char* en, const char* pt, const char* es, const char* ja, const char* zh) {
  if (lang == LANG_PT) return pt;
  if (lang == LANG_ES) return es;
  if (lang == LANG_JA) return ja;
  if (lang == LANG_ZH) return zh;
  return en;
}

static const char* localText(Language lang, const char* key) {
  if (strcmp(key, "select_language") == 0) return localChoice(lang, "Select language", "Selecionar idioma", "Seleccionar idioma", "言語を選択", "选择语言");
  if (strcmp(key, "toggle_theme") == 0) return localChoice(lang, "Toggle theme", "Alternar tema", "Alternar tema", "テーマを切り替え", "切换主题");
  if (strcmp(key, "nav_main") == 0) return localChoice(lang, "Main navigation", "Navegação principal", "Navegación principal", "メインナビゲーション", "主导航");
  if (strcmp(key, "title_dashboard") == 0) return "SafraSense Aqua";
  if (strcmp(key, "title_settings") == 0) return localChoice(lang, "Settings", "Configurações", "Configuración", "設定", "设置");
  if (strcmp(key, "title_manual") == 0) return localChoice(lang, "Guide - SafraSense Aqua", "Manual — SafraSense Aqua", "Manual — SafraSense Aqua", "ガイド — SafraSense Aqua", "手册 — SafraSense Aqua");
  if (strcmp(key, "title_raiznet") == 0) return "Raiznet - SafraSense";
  if (strcmp(key, "nav_home") == 0) return localChoice(lang, "Home", "Início", "Inicio", "ホーム", "首页");
  if (strcmp(key, "nav_raiznet") == 0) return "Raiznet";
  if (strcmp(key, "nav_config") == 0) return localChoice(lang, "Settings", "Configurações", "Configuración", "設定", "设置");
  if (strcmp(key, "nav_docs") == 0) return localChoice(lang, "Guide", "Manual", "Manual", "ガイド", "手册");
  if (strcmp(key, "overview_label") == 0) return localChoice(lang, "O V E R V I E W", "V I S Ã O   G E R A L", "V I S I Ó N   G E N E R A L", "総 覧", "总览");
  if (strcmp(key, "dashboard_empty_summary") == 0) return localChoice(lang, "Waiting for the first local sensor reading.", "Aguardando a primeira leitura local do sensor.", "Esperando la primera lectura local del sensor.", "最初のローカルセンサー読み取りを待機中。", "正在等待第一次本地传感器读数。");
  if (strcmp(key, "wifi_pending") == 0) return "Wi-Fi --";
  if (strcmp(key, "server_pending") == 0) return localChoice(lang, "Server --", "Servidor --", "Servidor --", "サーバー --", "服务器 --");
  if (strcmp(key, "buffer_pending") == 0) return "Buffer --";
  if (strcmp(key, "send_pending") == 0) return localChoice(lang, "Last send --", "Último envio --", "Último envío --", "最終送信 --", "上次发送 --");
  if (strcmp(key, "force_read") == 0) return localChoice(lang, "+ Take new reading", "+ Fazer nova leitura", "+ Hacer nueva lectura", "+ 新しい読み取りを実行", "+ 立即读取");
  if (strcmp(key, "metric_temp") == 0) return localChoice(lang, "Temperature", "Temperatura", "Temperatura", "温度", "温度");
  if (strcmp(key, "metric_humidity") == 0) return localChoice(lang, "Air humidity", "Umidade do ar", "Humedad del aire", "空気湿度", "空气湿度");
  if (strcmp(key, "metric_tds") == 0) return localChoice(lang, "Dissolved solids", "Sólidos dissolvidos", "Sólidos disueltos", "溶解固形物", "溶解固体");
  if (strcmp(key, "metric_ph") == 0) return localChoice(lang, "Hydrogen pot.", "Potencial Hidrog.", "Potencial hidrog.", "水素イオン指数", "氢离子浓度");
  if (strcmp(key, "metric_water") == 0) return localChoice(lang, "Water level", "Nível da água", "Nivel del agua", "水位", "水位");
  if (strcmp(key, "metric_battery") == 0) return localChoice(lang, "Battery", "Bateria", "Batería", "バッテリー", "电池");
  if (strcmp(key, "no_reading") == 0) return localChoice(lang, "no reading", "sem leitura", "sin lectura", "読み取りなし", "无读数");
  if (strcmp(key, "manual_input") == 0) return localChoice(lang, "manual input", "entrada manual", "entrada manual", "手動入力", "手动输入");
  if (strcmp(key, "servers_label") == 0) return localChoice(lang, "S E R V E R S", "S E R V I D O R E S", "S E R V I D O R E S", "サ ー バ ー", "服务器");
  if (strcmp(key, "external_label") == 0) return localChoice(lang, "E X T E R N A L", "E X T E R N O S", "E X T E R N O S", "外 部", "外部");
  if (strcmp(key, "local_label") == 0) return localChoice(lang, "L O C A L", "L O C A I S", "L O C A L E S", "ロ ー カ ル", "本地");
  if (strcmp(key, "system_label") == 0) return localChoice(lang, "S Y S T E M", "S I S T E M A", "S I S T E M A", "シ ス テ ム", "系统");
  if (strcmp(key, "config_label") == 0) return localChoice(lang, "S E T T I N G S", "C O N F I G U R A Ç Õ E S", "C O N F I G U R A C I Ó N", "設 定", "设置");
  if (strcmp(key, "config_heading") == 0) return localChoice(lang, "Destinations and System", "Destinos e Sistema", "Destinos y Sistema", "送信先とシステム", "目标与系统");
  if (strcmp(key, "sensor_name") == 0) return localChoice(lang, "Sensor name", "Nome do sensor", "Nombre del sensor", "センサー名", "传感器名称");
  if (strcmp(key, "public_servers") == 0) return localChoice(lang, "Public servers", "Servidores Públicos", "Servidores públicos", "公開サーバー", "公共服务器");
  if (strcmp(key, "local_server") == 0) return localChoice(lang, "Local server", "Servidor Local", "Servidor local", "ローカルサーバー", "本地服务器");
  if (strcmp(key, "other_btn") == 0) return localChoice(lang, "+ Other", "+ Outro", "+ Otro", "+ その他", "+ 其他");
  if (strcmp(key, "use_arateki") == 0) return localChoice(lang, "Use Arateki", "Usar Arateki", "Usar Arateki", "Aratekiを使用", "使用 Arateki");
  if (strcmp(key, "save") == 0) return localChoice(lang, "Save", "Salvar", "Guardar", "保存", "保存");
  if (strcmp(key, "tools") == 0) return localChoice(lang, "Tools", "Ferramentas", "Herramientas", "ツール", "工具");
  if (strcmp(key, "status_api") == 0) return "Status API";
  if (strcmp(key, "json") == 0) return "JSON";
  if (strcmp(key, "reconnect_wifi") == 0) return localChoice(lang, "Reconnect Wi-Fi", "Reconectar Wi-Fi", "Reconectar Wi-Fi", "Wi-Fiに再接続", "重新连接 Wi-Fi");
  if (strcmp(key, "reconnect_confirm") == 0) return localChoice(lang, "Reconnect Wi-Fi?", "Reconectar Wi-Fi?", "¿Reconectar Wi-Fi?", "Wi-Fiに再接続しますか？", "重新连接 Wi-Fi？");
  if (strcmp(key, "danger_zone") == 0) return localChoice(lang, "Danger zone", "Zona de perigo", "Zona de peligro", "危険ゾーン", "危险区域");
  if (strcmp(key, "factory_reset") == 0) return localChoice(lang, "Full Reset (Erase Keys)", "Reset Completo (Apagar Chaves)", "Reset completo (borrar claves)", "フルリセット（キーの消去）", "完全重置（删除密钥）");
  if (strcmp(key, "name_placeholder") == 0) return localChoice(lang, "Name", "Nome", "Nombre", "名前", "名称");
  if (strcmp(key, "url_or_ip_port_placeholder") == 0) return localChoice(lang, "URL or IP:port", "URL ou IP:porta", "URL o IP:puerto", "URL または IP:ポート", "URL 或 IP:端口");
  if (strcmp(key, "ip_port_placeholder") == 0) return localChoice(lang, "IP:port", "IP:porta", "IP:puerto", "IP:ポート", "IP:端口");
  if (strcmp(key, "url_placeholder") == 0) return "URL";
  if (strcmp(key, "manual_label") == 0) return localChoice(lang, "G U I D E", "M A N U A L", "M A N U A L", "ガ イ ド", "手册");
  if (strcmp(key, "docs_title") == 0) return localChoice(lang, "SafraSense Guide", "Guia SafraSense", "Guía SafraSense", "SafraSense ガイド", "SafraSense 指南");
  if (strcmp(key, "docs_subtitle") == 0) return localChoice(lang, "Quick reference for setup, monitoring, and hydroponic growing.", "Referência rápida para configuração, monitoramento e cultivo hidropônico.", "Referencia rápida para configuración, monitoreo y cultivo hidropónico.", "設定、監視、水耕栽培のクイックリファレンス。", "配置、监测和水培种植的快速参考。");
  if (strcmp(key, "copy_docs_title") == 0) return localChoice(lang, "Copy full guide", "Copiar manual completo", "Copiar manual completo", "ガイド全文をコピー", "复制完整手册");
  if (strcmp(key, "raiznet_label") == 0) return localChoice(lang, "D E C E N T R A L I Z E D   N E T W O R K", "R E D E   D E S C E N T R A L I Z A D A", "R E D   D E S C E N T R A L I Z A D A", "分 散 型 ネ ッ ト ワ ー ク", "去中心化网络");
  if (strcmp(key, "raiznet_heading") == 0) return localChoice(lang, "Raiznet Status", "Status Raiznet", "Estado Raiznet", "Raiznetステータス", "Raiznet 状态");
  if (strcmp(key, "connected_servers") == 0) return localChoice(lang, "Connected servers", "Servidores Conectados", "Servidores conectados", "接続済みサーバー", "已连接服务器");
  if (strcmp(key, "loading_status") == 0) return localChoice(lang, "Loading status...", "Carregando status...", "Cargando estado...", "ステータスを読み込み中...", "正在加载状态...");
  if (strcmp(key, "reset_wifi_title") == 0) return localChoice(lang, "Reconnecting Wi-Fi", "Reconectando Wi-Fi", "Reconectando Wi-Fi", "Wi-Fiに再接続中", "正在重新连接 Wi-Fi");
  if (strcmp(key, "reset_wifi_body") == 0) return localChoice(lang, "Wait a few seconds.", "Aguarde alguns segundos.", "Espere unos segundos.", "数秒お待ちください。", "请等待几秒钟。");
  if (strcmp(key, "factory_title") == 0) return localChoice(lang, "Factory reset", "Reset de fábrica", "Restablecimiento de fábrica", "工場出荷時リセット", "恢复出厂设置");
  if (strcmp(key, "factory_warning") == 0) return localChoice(lang, "This action will <strong>permanently erase</strong> the cryptographic identity and settings.", "Esta ação vai <strong>apagar permanentemente</strong> a identidade criptográfica e as configurações.", "Esta acción va a <strong>borrar permanentemente</strong> la identidad criptográfica y la configuración.", "この操作により、暗号化アイデンティティと設定が<strong>永久に消去</strong>されます。", "此操作将<strong>永久删除</strong>加密身份和设置。");
  if (strcmp(key, "factory_confirm_hint") == 0) return localChoice(lang, "To confirm, type <strong>CONFIRM</strong>:", "Para confirmar, digite <strong>CONFIRMAR</strong>:", "Para confirmar, escriba <strong>CONFIRMAR</strong>:", "確認のため、<strong>CONFIRM</strong> と入力してください：", "如需确认，请输入 <strong>CONFIRM</strong>：");
  if (strcmp(key, "factory_placeholder") == 0) return localChoice(lang, "CONFIRM", "CONFIRMAR", "CONFIRMAR", "CONFIRM", "CONFIRM");
  if (strcmp(key, "factory_button") == 0) return localChoice(lang, "Erase and restart", "Apagar e reiniciar", "Borrar y reiniciar", "消去して再起動", "删除并重启");
  if (strcmp(key, "factory_back") == 0) return localChoice(lang, "← Back", "← Voltar", "← Volver", "← 戻る", "← 返回");
  if (strcmp(key, "factory_running_title") == 0) return localChoice(lang, "Resetting...", "Resetando...", "Restableciendo...", "リセット中...", "正在重置...");
  if (strcmp(key, "factory_running_body") == 0) return localChoice(lang, "The device will restart and generate a new identity.", "O dispositivo vai reiniciar e gerar uma nova identidade.", "El dispositivo se reiniciará y generará una nueva identidad.", "デバイスが再起動し、新しいアイデンティティが生成されます。", "设备将重启并生成新的身份。");
  return key;
}

static const char* LOCAL_I18N_KEYS[] = {
  "select_language", "toggle_theme", "nav_main", "nav_home", "nav_raiznet",
  "nav_config", "nav_docs", "overview_label", "dashboard_empty_summary",
  "wifi_pending", "server_pending", "buffer_pending", "send_pending",
  "force_read", "metric_temp", "metric_humidity", "metric_tds", "metric_ph",
  "metric_water", "metric_battery", "no_reading", "manual_input",
  "servers_label", "external_label", "local_label", "system_label",
  "config_label", "config_heading", "sensor_name", "public_servers",
  "local_server", "other_btn", "use_arateki", "save", "tools",
  "status_api", "json", "reconnect_wifi", "danger_zone", "factory_reset",
  "name_placeholder", "url_or_ip_port_placeholder", "ip_port_placeholder",
  "url_placeholder", "manual_label", "docs_title", "docs_subtitle",
  "copy_docs_title", "raiznet_label", "raiznet_heading", "connected_servers",
  "loading_status"
};

static void replaceBetween(String& html, const String& open, const String& close, const String& value) {
  int start = html.indexOf(open);
  if (start < 0) return;
  start += open.length();
  int end = html.indexOf(close, start);
  if (end < 0) return;
  html = html.substring(0, start) + value + html.substring(end);
}

static void localizeMarkedText(String& html, const char* key, const char* value) {
  String marker = String("data-i18n=\"") + key + "\">";
  int search = 0;
  while (true) {
    int pos = html.indexOf(marker, search);
    if (pos < 0) break;
    int textStart = pos + marker.length();
    int textEnd = html.indexOf('<', textStart);
    if (textEnd < 0) break;
    html = html.substring(0, textStart) + String(value) + html.substring(textEnd);
    search = textStart + strlen(value);
  }
}

static void localizeMarkedAttr(String& html, const char* dataAttr, const char* attr, const char* key, const char* value) {
  String marker = String(dataAttr) + "=\"" + key + "\"";
  String attrMarker = String(attr) + "=\"";
  int search = 0;
  while (true) {
    int pos = html.indexOf(marker, search);
    if (pos < 0) break;
    int tagStart = html.lastIndexOf('<', pos);
    if (tagStart < 0) break;
    int attrPos = html.indexOf(attrMarker, tagStart);
    if (attrPos >= 0 && attrPos < pos) {
      int valueStart = attrPos + attrMarker.length();
      int valueEnd = html.indexOf('"', valueStart);
      if (valueEnd >= 0 && valueEnd < pos) {
        html = html.substring(0, valueStart) + String(value) + html.substring(valueEnd);
        search = pos + strlen(value);
        continue;
      }
    }
    search = pos + marker.length();
  }
}

static void localizeLocalHtml(String& html, Language lang, const char* titleKey) {
  html.replace("<html lang=\"pt-BR\">", String("<html lang=\"") + localHtmlLang(lang) + "\">");
  if (titleKey) replaceBetween(html, "<title>", "</title>", localText(lang, titleKey));
  for (size_t i = 0; i < sizeof(LOCAL_I18N_KEYS) / sizeof(LOCAL_I18N_KEYS[0]); i++) {
    const char* key = LOCAL_I18N_KEYS[i];
    const char* value = localText(lang, key);
    localizeMarkedText(html, key, value);
    localizeMarkedAttr(html, "data-i18n-placeholder", "placeholder", key, value);
    localizeMarkedAttr(html, "data-i18n-title", "title", key, value);
    localizeMarkedAttr(html, "data-i18n-aria-label", "aria-label", key, value);
  }
}

const char LOCAL_PORTAL_CSS[] PROGMEM = R"rawliteral(
:root{
  --bg:#f4f1ea;--bg-2:#ede8dc;--bg-card:#fbf8f1;--bg-inset:#e8e2d2;
  --fg:#1d231e;--fg-2:#46493d;--fg-3:#807d6e;--fg-4:#b3ad9c;
  --line:#d8d2bf;--line-strong:#1d231e;--paper-tint:#f7f1de;
  --primary:#1a3a28;--primary-soft:rgba(26,58,40,.12);--aqua:#9ed8ff;
  --good:#2f7d45;--warn:#b8651e;--bad:#a83a2a;
  --f-sans:-apple-system,BlinkMacSystemFont,"Segoe UI",system-ui,sans-serif;
  --f-mono:"SF Mono","JetBrains Mono",ui-monospace,Menlo,monospace;
  --f-serif:Georgia,"Times New Roman",serif;
}
[data-theme="dark"]{
  --bg:#0d1310;--bg-2:#121814;--bg-card:#161d18;--bg-inset:#0a0f0c;
  --fg:#d8e3d4;--fg-2:#9aa897;--fg-3:#6c7869;--fg-4:#3f4a3e;
  --line:#20281f;--line-strong:#d8e3d4;--paper-tint:#14201a;
  --primary:#2d6e4a;--primary-soft:rgba(45,110,74,.28);--aqua:#a8dcff;
  --good:#7fd08d;--warn:#d4933a;--bad:#d36e63;
}
*{box-sizing:border-box}
html,body{margin:0;min-height:100%;background:var(--bg);color:var(--fg)}
body{font-family:var(--f-sans);font-size:15px}
a{color:inherit;text-decoration:none}
button{font:inherit}
.serif{font-family:var(--f-serif);font-weight:400}
.mono{font-family:var(--f-mono)}
.eyebrow,.eyebrow-tight{font-weight:750;text-transform:uppercase;color:var(--fg-3);white-space:pre;letter-spacing:.18em}
.eyebrow{font-size:11px;line-height:1}.eyebrow-tight{font-size:10px;line-height:1;letter-spacing:.14em}
.local-header{position:fixed;top:0;left:0;right:0;height:68px;background:var(--bg);border-bottom:1px solid var(--line);z-index:50;display:grid;grid-template-columns:minmax(0,1fr) auto minmax(0,1fr);align-items:center;padding:0 24px}
.header-actions{justify-self:end;display:flex;align-items:center;gap:4px}
.lang-select{height:34px;background:transparent;border:1px solid var(--line);border-radius:3px;color:var(--fg);font-size:12px;font-weight:750;text-transform:uppercase;cursor:pointer;padding:0 8px;appearance:none;text-align:center}
.lang-select:focus{outline:none}
.lang-select option{background:var(--bg);color:var(--fg)}
.local-brand{justify-self:start;min-width:0;color:var(--fg);overflow:hidden;white-space:nowrap}
.local-brand-title{display:block;font-size:12px;font-weight:850;letter-spacing:.16em;text-transform:uppercase;overflow:hidden;text-overflow:ellipsis}
.local-brand-title .brand-aqua{color:var(--aqua)}
.local-tabs{justify-self:center;display:flex;align-items:flex-end;justify-content:center;gap:10px;border-bottom:0;background:transparent;padding:0;position:relative;z-index:10}
.local-tab{display:inline-flex;width:auto;margin:0 0 -1px;padding:6px 14px 7px;background:transparent;color:var(--primary);border:1px solid var(--primary);border-bottom:2px solid var(--primary);border-radius:4px 4px 0 0;font-size:12px;font-weight:750;letter-spacing:.08em;text-transform:uppercase;transition:transform .12s ease-out;position:relative;z-index:11}
.local-tab:hover{transform:scale(1.04)}
.local-tab:active{transform:scale(.96)}
.local-tab.is-active{background:transparent;color:var(--primary);border-width:2.5px;border-bottom-width:5px;font-weight:900;transform:scale(1.1)}
.theme-btn.local-theme{justify-self:end;width:34px;height:34px;margin:0;padding:0;display:flex;align-items:center;justify-content:center;background:transparent;border:1px solid var(--line);border-radius:3px;color:var(--fg);font-size:12px;transition:transform .08s ease}
.theme-btn.local-theme:active{transform:scale(.88)}
.theme-btn.local-theme svg{width:15px;height:15px;display:block}
#loader-overlay{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:var(--bg);opacity:.85;z-index:9999}
#loader-overlay::after{content:"•••";position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);color:var(--fg);font-size:32px;letter-spacing:4px;animation:blink 1.4s infinite both}
@keyframes blink{0%{opacity:.2}20%{opacity:1}100%{opacity:.2}}
body.is-loading{pointer-events:none!important;overflow:hidden}
body.is-loading #loader-overlay{display:block}
.portal-shell{min-height:100vh;padding:104px 42px 32px}
.main{width:100%;max-width:1120px;margin:0 auto;min-width:0;padding:0}
.topbar{display:flex;justify-content:space-between;gap:18px;align-items:flex-start;margin-bottom:32px}
.title h1{margin:8px 0 0;font-size:38px;line-height:1.08}
.title p{margin:10px 0 0;color:var(--fg-2);font-size:14px;font-weight:500;line-height:1.5;max-width:720px;overflow-wrap:anywhere}
.copy-btn{background:transparent;border:none;color:var(--fg-3);cursor:pointer;padding:0;margin-left:6px;vertical-align:middle;display:inline-flex;align-items:center;justify-content:center}
.copy-btn:hover{color:var(--fg)}
.copy-btn.copied{color:var(--good)}
.copy-btn svg{width:14px;height:14px}
.btn,.theme-btn{border:1px solid var(--line-strong);background:transparent;color:var(--fg);border-radius:2px;padding:9px 13px;font-size:12px;font-weight:750;letter-spacing:.04em;cursor:pointer;text-transform:uppercase;transition:transform .12s ease-out}
.btn:hover:not(:disabled),.theme-btn:hover:not(:disabled){transform:scale(1.04)}
.btn:active:not(:disabled),.theme-btn:active:not(:disabled){transform:scale(.96)}
.btn:disabled{cursor:not-allowed;opacity:0.5}
.btn-primary{background:var(--primary);border-color:var(--primary);color:#f4f1ea}
.status-strip{display:flex;flex-wrap:wrap;justify-content:center;gap:10px;margin-bottom:26px;text-align:center}
.status-pill{display:inline-flex;align-items:center;gap:7px;border:1px solid var(--line);background:var(--bg-card);padding:7px 10px;font-size:12px;font-weight:650;color:var(--fg-2)}
.status-light{width:7px;height:7px;background:var(--fg-4)}
.status-pill.ok .status-light{background:var(--good)}.status-pill.warn .status-light{background:var(--warn)}.status-pill.bad .status-light{background:var(--bad)}
.metric-grid{display:grid;grid-template-columns:repeat(6,minmax(0,1fr));gap:1px;background:var(--line);border:1px solid var(--line);margin-bottom:34px}
@media(max-width:1100px){.metric-grid{grid-template-columns:repeat(3,minmax(0,1fr))}}
.metric-card{min-width:0;background:var(--bg-card);padding:18px 18px 16px;position:relative;cursor:pointer}
.metric-card:focus{outline:2px solid var(--primary);outline-offset:-2px}
.metric-value{font-family:var(--f-serif);font-size:34px;line-height:1;margin-top:12px;white-space:nowrap}
.metric-unit{font-family:var(--f-sans);font-size:14px;font-weight:650;color:var(--fg-3);margin-left:4px}
.metric-detail{font-size:11px;font-weight:650;color:var(--fg-3);margin-top:8px;min-height:1.2em}
.metric-card.is-good .metric-detail{color:var(--good)}.metric-card.is-warn .metric-detail{color:var(--warn)}.metric-card.is-bad .metric-detail{color:var(--bad)}
.metric-help{display:none;margin-top:12px;padding-top:10px;border-top:1px solid var(--line);font-size:12px;line-height:1.45;color:var(--fg-2);font-weight:500}
.metric-help strong{display:block;margin-bottom:4px;color:var(--fg);font-size:11px;text-transform:uppercase;letter-spacing:.06em}
.metric-range{display:block;margin-top:6px;color:var(--fg);font-size:11px;font-weight:750}
.metric-card.is-help-open .metric-help{display:block}
.content-grid{display:grid;grid-template-columns:minmax(0,1fr) minmax(320px,.8fr);gap:36px;align-items:start}
.section-head{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:14px}
.panel{border-top:1px solid var(--line-strong);padding-top:14px}
.info-list{border-top:1px solid var(--line)}
.info-row{display:flex;justify-content:space-between;gap:14px;border-bottom:1px solid var(--line);padding:10px 0;font-size:12px;font-weight:650}
.info-row span:first-child{color:var(--fg-2)}.info-row span:last-child{font-family:var(--f-mono);text-align:right;word-break:break-all}
.server-list{display:flex;flex-direction:column;gap:8px;margin-top:12px}
.server-chip{border:1px solid var(--line);background:var(--paper-tint);padding:9px 10px;font-size:12px;font-weight:650}
.server-chip-top{display:flex;align-items:center;justify-content:space-between;gap:10px}
.server-chip strong{display:block;min-width:0;font-size:12px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.server-chip span{display:block;margin-top:3px;color:var(--fg-3);font-family:var(--f-mono);font-size:11px;word-break:break-all}
.server-status{display:inline-flex;align-items:center;gap:5px;flex:0 0 auto;color:var(--fg-3);font-size:10px;font-weight:800;text-transform:uppercase;letter-spacing:.06em}
.server-status::before{content:"";width:7px;height:7px;background:var(--fg-4)}
.server-status.ok{color:var(--good)}.server-status.ok::before{background:var(--good)}
.server-status.bad{color:var(--bad)}.server-status.bad::before{background:var(--bad)}
.empty{color:var(--fg-3);font-size:12px;font-weight:650;border:1px dashed var(--line);padding:10px}
.form-label{display:block;font-size:10px;color:var(--fg-3);margin-bottom:6px;text-transform:uppercase;letter-spacing:.05em;font-weight:750}
.form-input{width:100%;padding:12px 14px;background:var(--bg-inset);border:1px solid var(--line);border-radius:2px;color:var(--fg);font-size:13px;font-family:var(--f-mono);margin-bottom:16px}
.form-input:focus{border-color:var(--primary);outline:none}
.srow{display:flex;gap:6px;margin-bottom:10px;align-items:center}
.srow .form-input{margin-bottom:0;flex:1}
.btn-danger{color:var(--bad);border-color:var(--bad)}
@media(max-width:900px){
  .local-header{grid-template-columns:minmax(0,1fr) auto;grid-template-rows:28px 24px;height:62px;padding:3px 12px 2px;gap:0 8px}
  .header-actions{grid-column:2;grid-row:1;align-self:center}
  .local-brand-title{font-size:10px;line-height:1;letter-spacing:.04em}
  .local-tabs{grid-column:1 / -1;grid-row:2;justify-self:center;align-self:start;gap:8px}
  .local-tab{display:inline-flex;width:auto;margin:0 0 -1px;padding:4px 9px 5px;background:transparent;color:var(--primary);border:1px solid var(--primary);border-bottom:2px solid var(--primary);border-radius:4px 4px 0 0;font-size:10px;font-weight:750;letter-spacing:.08em}
  .local-tab.is-active{background:transparent;color:var(--primary);border-width:2px;border-bottom-width:4px;font-weight:900;transform:scale(1.1);z-index:2}
  .theme-btn.local-theme{width:28px;height:28px}
  .theme-btn.local-theme svg{width:16px;height:16px}
  .lang-select{height:28px;padding:0 6px;font-size:11px}
  .portal-shell{display:block;padding:96px 20px 28px}.topbar{display:block}
  .dev-sep{display:none}.dev-key{display:block;margin-top:4px}
  .metric-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.content-grid{grid-template-columns:1fr}
}
@media(max-width:520px){
  .metric-grid{grid-template-columns:repeat(2,minmax(0,1fr))}
  .metric-card{padding:14px 12px 13px}
  .metric-value{font-size:30px}
  .metric-unit{font-size:12px}
  .title h1{font-size:31px}.local-brand-title{font-size:9px;letter-spacing:.02em}
}
.doc-wrap{max-width:740px}
.doc-section{border-top:1px solid var(--line);margin:0}
details.doc-section summary{list-style:none;display:flex;align-items:center;justify-content:space-between;padding:17px 0;cursor:pointer;font-size:15px;font-weight:850;text-transform:uppercase;letter-spacing:.06em;color:var(--fg)}
details.doc-section summary::-webkit-details-marker{display:none}
details.doc-section summary::after{content:'+';font-size:16px;font-weight:300;color:var(--fg-3);flex-shrink:0;margin-left:8px}
details.doc-section[open] summary::after{content:'\2212'}
.doc-body{padding-bottom:18px;font-size:17px;font-weight:550;line-height:1.68;color:var(--fg-2)}
.doc-h4{font-size:14px;font-weight:850;text-transform:uppercase;letter-spacing:.06em;color:var(--fg-3);margin:18px 0 8px}
.doc-body p{margin:0 0 10px}
.doc-body ul,.doc-body ol{margin:6px 0 10px;padding-left:20px}
.doc-body li{margin-bottom:5px}
.doc-body strong{font-weight:850;color:var(--fg)}
.doc-body a{color:var(--primary);font-size:inherit;font-weight:850;letter-spacing:0;text-transform:none;text-decoration:underline}
.doc-badge{display:inline-block;font-family:var(--f-mono);font-size:14px;background:var(--primary-soft);color:var(--primary);padding:2px 7px;border-radius:2px;font-weight:750}
.doc-good{background:rgba(47,125,69,.13)!important;color:var(--good)!important}
.doc-warn{background:rgba(184,101,30,.13)!important;color:var(--warn)!important}
.doc-bad{background:rgba(168,58,42,.13)!important;color:var(--bad)!important}
.doc-body table{width:100%;border-collapse:collapse;font-size:15px;margin:10px 0 14px}
.doc-body th{text-align:left;font-size:13px;font-weight:850;text-transform:uppercase;letter-spacing:.04em;color:var(--fg-3);border-bottom:2px solid var(--line);padding:8px 8px}
.doc-body td{padding:9px 8px;border-bottom:1px solid var(--line);color:var(--fg-2);font-weight:550}
.doc-body td:first-child{font-weight:800;color:var(--fg);font-size:15px}
.doc-body dl{margin:0}
.doc-body dt{font-family:var(--f-mono);font-size:17px;font-weight:900;color:var(--fg);margin-top:16px}
.doc-body dd{margin:5px 0 0;font-size:16px;font-weight:550;color:var(--fg-2);line-height:1.64}
.doc-body code{font-family:var(--f-mono);font-size:15px;background:var(--bg-inset);border:1px solid var(--line);padding:0 4px;border-radius:2px}
.doc-toc{margin-bottom:18px;padding:11px 14px;background:var(--bg-inset);border:1px solid var(--line);border-radius:3px}
.doc-toc-item{border-top:0}
.doc-h4+.doc-toc-item{border-top:0}
.doc-toc-row{display:flex;align-items:center;gap:7px}
.doc-toc a{display:block;font-size:16px;font-weight:750;color:var(--primary);text-decoration:none;padding:7px 0}
.doc-toc a:hover{text-decoration:underline}
.doc-toc-toggle{appearance:none;background:transparent;border:1px solid var(--line);border-radius:5px;color:var(--fg-3);cursor:pointer;width:30px;height:30px;padding:0;font-size:18px;font-weight:650;line-height:1;display:flex;align-items:center;justify-content:center;box-shadow:0 1px 2px rgba(0,0,0,0.05);transition:transform .12s, border-color .12s}
.doc-toc-toggle:hover,.doc-toc-toggle:focus{border-color:var(--primary);color:var(--fg);outline:none;transform:scale(1.05)}
.doc-toc-item.is-open .doc-toc-toggle{border-color:var(--primary);color:var(--primary);box-shadow:inset 0 1px 2px rgba(0,0,0,0.08)}
.doc-subtoc{margin:-1px 0 5px 5px;padding:0 0 5px 12px;border-left:1px solid var(--line)}
.doc-subtoc[hidden]{display:none}
.doc-subtoc a{font-size:13px;font-weight:650;line-height:1.25;color:var(--fg-3);padding:5px 0}
.doc-subtoc a:hover{color:var(--fg)}
.doc-section,.doc-h4{scroll-margin-top:88px}
@media(min-width:901px){
  .doc-body{font-weight:430}
  .doc-body td{font-weight:430}
  .doc-body dd{font-weight:430}
}
)rawliteral";

const char LOCAL_DASHBOARD_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SafraSense Aqua</title>
<script>(function(){try{var u=new URL(location.href);if(!u.searchParams.has('lang')){var l=localStorage.getItem('lang');if(l){l=String(l).toLowerCase();l=(l==='1'||l==='pt')?'1':((l==='2'||l==='es')?'2':((l==='3'||l==='ja')?'3':((l==='4'||l==='zh'||l.indexOf('zh-')===0)?'4':'0')));u.searchParams.set('lang',l);location.replace(u.pathname+u.search+u.hash);return;}}}catch(_){}var t='light';try{t=localStorage.getItem('theme')||(window.matchMedia&&window.matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light');}catch(_){}document.documentElement.setAttribute('data-theme',t);})();</script>
<link rel="stylesheet" href="/local.css">
</head>
<body>
<header class="local-header">
  <a class="local-brand" href="/">
    <span class="local-brand-title">S A F R A S E N S E <span class="brand-aqua">A Q U A</span></span>
  </a>
  <nav class="local-tabs" aria-label="Navegação principal" data-i18n-aria-label="nav_main">
    <a class="local-tab" href="/" data-i18n="nav_home">Início</a>
    <a class="local-tab" id="raiznet-menu-item" href="/raiznet" style="display:none" data-i18n="nav_raiznet">Raiznet</a>
    <a class="local-tab" href="/config" data-i18n="nav_config">Configurações</a>
    <a class="local-tab" href="/docs" data-i18n="nav_docs">Manual</a>
  </nav>
  <div class="header-actions">
    <select class="lang-select" id="langSelect" aria-label="Selecionar idioma" data-i18n-aria-label="select_language">
      <option value="1">PT</option>
      <option value="0">EN</option>
      <option value="2">ES</option>
      <option value="3">JA</option>
      <option value="4">ZH</option>
    </select>
    <button class="theme-btn local-theme" id="themeBtn" type="button" aria-label="Alternar tema" data-i18n-aria-label="toggle_theme"></button>
  </div>
</header>
<div class="portal-shell">
  <main class="main">
    <div class="topbar">
      <div class="title">
        <div class="eyebrow" data-i18n="overview_label">V I S Ã O   G E R A L</div>
        <h1 class="serif" id="deviceName">SafraSense Aqua</h1>
        <p id="deviceSummary" data-i18n="dashboard_empty_summary">Aguardando a primeira leitura local do sensor.</p>
      </div>
    </div>

    <div class="status-strip">
      <div class="status-pill" id="wifiPill"><span class="status-light"></span><span data-i18n="wifi_pending">Wi-Fi --</span></div>
      <div class="status-pill" id="serverPill"><span class="status-light"></span><span data-i18n="server_pending">Servidor --</span></div>
      <div class="status-pill" id="bufferPill"><span class="status-light"></span><span data-i18n="buffer_pending">Buffer --</span></div>
      <div class="status-pill" id="sendPill"><span class="status-light"></span><span data-i18n="send_pending">Último envio --</span></div>
    </div>

    <div style="display:flex;justify-content:center;margin:0 0 14px">
      <button class="btn" id="forceReadBtn" onclick="forceRead()" style="font-size:10px;padding:6px 12px" data-i18n="force_read">+ Fazer nova leitura</button>
    </div>

    <section class="metric-grid" id="metricGrid">
      <article class="metric-card" id="mTemp"><div class="eyebrow-tight" data-i18n="metric_temp">Temperatura</div><div class="metric-value"><span data-value>--</span><span class="metric-unit">°C</span></div><div class="metric-detail" data-detail data-i18n="no_reading">sem leitura</div></article>
      <article class="metric-card" id="mHum"><div class="eyebrow-tight" data-i18n="metric_humidity">Umidade do ar</div><div class="metric-value"><span data-value>--</span><span class="metric-unit">%</span></div><div class="metric-detail" data-detail data-i18n="no_reading">sem leitura</div></article>
      <article class="metric-card" id="mEc"><div class="eyebrow-tight" data-i18n="metric_tds">Sólidos dissolvidos</div><div class="metric-value"><span data-value>--</span><span class="metric-unit">ppm</span></div><div class="metric-detail" data-detail data-i18n="no_reading">sem leitura</div></article>
      <article class="metric-card" id="mPh">
        <div class="eyebrow-tight" data-i18n="metric_ph">Potencial Hidrog.</div>
        <div class="metric-value" style="display:flex;align-items:baseline;justify-content:space-between">
          <div><span data-value>--</span><span class="metric-unit">pH</span></div>
          <button class="copy-btn" onclick="event.stopPropagation();manualPh()" title="Inserir manual" data-i18n-title="manual_input" style="margin:0;padding:4px"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"></path><path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"></path></svg></button>
        </div>
        <div class="metric-detail" data-detail data-i18n="manual_input">entrada manual</div>
      </article>
      <article class="metric-card" id="mWater"><div class="eyebrow-tight" data-i18n="metric_water">Nível da água</div><div class="metric-value"><span data-value>--</span><span class="metric-unit">cm</span></div><div class="metric-detail" data-detail data-i18n="no_reading">sem leitura</div></article>
      <article class="metric-card" id="mBattery"><div class="eyebrow-tight" data-i18n="metric_battery">Bateria</div><div class="metric-value"><span data-value>--</span><span class="metric-unit">%</span></div><div class="metric-detail" data-detail data-i18n="no_reading">sem leitura</div></article>
    </section>
    <section class="content-grid">
      <div>
        <div class="section-head"><div class="eyebrow" data-i18n="servers_label">S E R V I D O R E S</div></div>
        <div class="panel">
          <div class="eyebrow-tight" data-i18n="external_label">E X T E R N O S</div>
          <div class="server-list" id="externalServers"></div>
          <div class="eyebrow-tight" style="margin-top:16px" data-i18n="local_label">L O C A I S</div>
          <div class="server-list" id="localServers"></div>
        </div>
      </div>

      <div>
        <div class="section-head"><div class="eyebrow" data-i18n="system_label">S I S T E M A</div></div>
        <div class="panel">
          <div class="info-list" id="systemInfo"></div>
        </div>
      </div>
    </section>
  </main>
</div>
<script src="/local-nav.js"></script>
<script src="/dashboard.js"></script>
</body>
</html>)rawliteral";

const char LOCAL_NAV_JS[] PROGMEM = R"rawliteral(
(function(){
  var dict={
    '0':{
      select_language:'Select language',toggle_theme:'Toggle theme',nav_main:'Main navigation',
      title_dashboard:'SafraSense Aqua',title_settings:'Settings',title_manual:'Guide - SafraSense Aqua',title_raiznet:'Raiznet - SafraSense',
      nav_home:'Home',nav_raiznet:'Raiznet',nav_config:'Settings',nav_docs:'Guide',
      overview_label:'O V E R V I E W',dashboard_empty_summary:'Waiting for the first local sensor reading.',
      wifi_pending:'Wi-Fi --',server_pending:'Server --',buffer_pending:'Buffer --',send_pending:'Last send --',
      force_read:'+ Take new reading',reading_sensors:'Reading sensors...',
      metric_temp:'Temperature',metric_humidity:'Air humidity',metric_tds:'Dissolved solids',
      metric_ph:'Hydrogen pot.',metric_water:'Water level',metric_battery:'Battery',
      no_reading:'no reading',manual_input:'manual input',servers_label:'S E R V E R S',
      external_label:'E X T E R N A L',local_label:'L O C A L',system_label:'S Y S T E M',
      config_label:'S E T T I N G S',config_heading:'Destinations and System',sensor_name:'Sensor name',
      public_servers:'Public servers',local_server:'Local server',other_btn:'+ Other',use_arateki:'Use Arateki',
      save:'Save',tools:'Tools',status_api:'Status API',json:'JSON',reconnect_wifi:'Reconnect Wi-Fi',
      reconnect_confirm:'Reconnect Wi-Fi?',danger_zone:'Danger zone',factory_reset:'Full Reset (Erase Keys)',
      name_placeholder:'Name',url_or_ip_port_placeholder:'URL or IP:port',ip_port_placeholder:'IP:port',url_placeholder:'URL',
      manual_label:'G U I D E',docs_title:'SafraSense Guide',
      docs_subtitle:'Quick reference for setup, monitoring, and hydroponic growing.',copy_docs_title:'Copy full guide',
      raiznet_label:'D E C E N T R A L I Z E D   N E T W O R K',raiznet_heading:'Raiznet Status',
      connected_servers:'Connected servers',loading_status:'Loading status...',
      wifi_connected:'Wi-Fi connected',wifi_offline:'Wi-Fi offline',server_online:'Server online',
      server_offline:'Server offline',pending_suffix:' pending',last_send:'Last send ',local_no_response:'No local response',
      sensor_offline:'sensor offline',dht_active:'DHT active',tds_active:'TDS active',laser_active:'laser active',
      public_key:'Public key',copy:'Copy',copy_public_key:'Copy public key',
      uptime:'Uptime',free_heap:'Free heap',minutes_suffix:' min',kb_suffix:' KB',
      no_servers:'No server configured',server_default:'Server',online:'online',offline:'offline',
      ph_prompt:'Type the PH value (0-14):',invalid_ph_alert:'Invalid value. Enter a number between 0 and 14.',
      show_details:'Show details: ',
      help_temp_title:'Temperature',help_temp_text:'Shows the heat around the plant. Temperatures outside the ideal range reduce growth, water absorption, and nutrient response.',help_temp_range:'General ideal range: 20 to 28 °C.',
      help_hum_title:'Air humidity',help_hum_text:'Shows how much moisture is in the air. Very low humidity increases water loss; very high humidity favors fungi and makes plant transpiration harder.',help_hum_range:'General ideal range: 50 to 70%.',
      help_ec_title:'Dissolved solids',help_ec_text:'Estimates the amount of salts and nutrients dissolved in the water. Low values indicate weak nutrition; high values can stress roots.',help_ec_range:'General ideal range: 500 to 1200 ppm, depending on the crop.',
      help_ph_title:'Hydrogen potential',help_ph_text:'Measures water acidity or alkalinity. Correct pH is crucial so the plant can absorb nutrients in the solution.',help_ph_range:'General ideal range: 5.5 to 6.5.',
      help_water_title:'Water level',help_water_text:'Shows the available height in the reservoir. A low level can dry roots, stop circulation, or over-concentrate nutrients.',help_water_range:'Ideal range: above the reservoir safe minimum.',
      help_battery_title:'Battery',help_battery_text:'Shows the remaining device energy. Low battery can interrupt readings and delay data delivery to servers.',help_battery_range:'Ideal range: above 40%.'
    },
    '1':{
      select_language:'Selecionar idioma',toggle_theme:'Alternar tema',nav_main:'Navegação principal',
      title_dashboard:'SafraSense Aqua',title_settings:'Configurações',title_manual:'Manual — SafraSense Aqua',title_raiznet:'Raiznet - SafraSense',
      nav_home:'Início',nav_raiznet:'Raiznet',nav_config:'Configurações',nav_docs:'Manual',
      overview_label:'V I S Ã O   G E R A L',dashboard_empty_summary:'Aguardando a primeira leitura local do sensor.',
      wifi_pending:'Wi-Fi --',server_pending:'Servidor --',buffer_pending:'Buffer --',send_pending:'Último envio --',
      force_read:'+ Fazer nova leitura',reading_sensors:'Lendo sensores...',
      metric_temp:'Temperatura',metric_humidity:'Umidade do ar',metric_tds:'Sólidos dissolvidos',
      metric_ph:'Potencial Hidrog.',metric_water:'Nível da água',metric_battery:'Bateria',
      no_reading:'sem leitura',manual_input:'entrada manual',servers_label:'S E R V I D O R E S',
      external_label:'E X T E R N O S',local_label:'L O C A I S',system_label:'S I S T E M A',
      config_label:'C O N F I G U R A Ç Õ E S',config_heading:'Destinos e Sistema',sensor_name:'Nome do sensor',
      public_servers:'Servidores Públicos',local_server:'Servidor Local',other_btn:'+ Outro',use_arateki:'Usar Arateki',
      save:'Salvar',tools:'Ferramentas',status_api:'Status API',json:'JSON',reconnect_wifi:'Reconectar Wi-Fi',
      reconnect_confirm:'Reconectar Wi-Fi?',danger_zone:'Zona de perigo',factory_reset:'Reset Completo (Apagar Chaves)',
      name_placeholder:'Nome',url_or_ip_port_placeholder:'URL ou IP:porta',ip_port_placeholder:'IP:porta',url_placeholder:'URL',
      manual_label:'M A N U A L',docs_title:'Guia SafraSense',
      docs_subtitle:'Referência rápida para configuração, monitoramento e cultivo hidropônico.',copy_docs_title:'Copiar manual completo',
      raiznet_label:'R E D E   D E S C E N T R A L I Z A D A',raiznet_heading:'Status Raiznet',
      connected_servers:'Servidores Conectados',loading_status:'Carregando status...',
      wifi_connected:'Wi-Fi conectado',wifi_offline:'Wi-Fi offline',server_online:'Servidor online',
      server_offline:'Servidor offline',pending_suffix:' pendente(s)',last_send:'Último envio ',local_no_response:'Sem resposta local',
      sensor_offline:'sensor offline',dht_active:'DHT ativo',tds_active:'TDS ativo',laser_active:'laser ativo',
      public_key:'Chave pública',copy:'Copiar',copy_public_key:'Copiar chave pública',
      uptime:'Uptime',free_heap:'Heap livre',minutes_suffix:' min',kb_suffix:' KB',
      no_servers:'Nenhum servidor configurado',server_default:'Servidor',online:'online',offline:'offline',
      ph_prompt:'Digite o valor do PH (0-14):',invalid_ph_alert:'Valor inválido. Insira um número entre 0 e 14.',
      show_details:'Mostrar detalhes: ',
      help_temp_title:'Temperatura',help_temp_text:'Indica o calor do ambiente ao redor da planta. Temperaturas fora da faixa ideal reduzem crescimento, absorção de água e resposta aos nutrientes.',help_temp_range:'Faixa ideal geral: 20 a 28 °C.',
      help_hum_title:'Umidade do ar',help_hum_text:'Mostra quanta umidade existe no ar. Umidade muito baixa aumenta perda de água; muito alta favorece fungos e dificulta a transpiração da planta.',help_hum_range:'Faixa ideal geral: 50 a 70%.',
      help_ec_title:'Sólidos dissolvidos',help_ec_text:'Estima a quantidade de sais e nutrientes dissolvidos na água. Valores baixos indicam pouca nutrição; valores altos podem causar estresse nas raízes.',help_ec_range:'Faixa ideal geral: 500 a 1200 ppm, conforme a cultura.',
      help_ph_title:'Potencial Hidrogeniônico',help_ph_text:'Mede a acidez ou alcalinidade da água. O pH correto é crucial para que a planta consiga absorver os nutrientes presentes na solução.',help_ph_range:'Faixa ideal geral: 5.5 a 6.5.',
      help_water_title:'Nível da água',help_water_text:'Mostra a altura disponível no reservatório. Nível baixo pode secar raízes, parar circulação ou concentrar demais os nutrientes.',help_water_range:'Faixa ideal: acima do mínimo seguro do reservatório.',
      help_battery_title:'Bateria',help_battery_text:'Indica a energia restante do dispositivo. Bateria baixa pode interromper leituras e atrasar o envio dos dados para os servidores.',help_battery_range:'Faixa ideal: acima de 40%.'
    },
    '2':{
      select_language:'Seleccionar idioma',toggle_theme:'Alternar tema',nav_main:'Navegación principal',
      title_dashboard:'SafraSense Aqua',title_settings:'Configuración',title_manual:'Manual — SafraSense Aqua',title_raiznet:'Raiznet - SafraSense',
      nav_home:'Inicio',nav_raiznet:'Raiznet',nav_config:'Configuración',nav_docs:'Manual',
      overview_label:'V I S I Ó N   G E N E R A L',dashboard_empty_summary:'Esperando la primera lectura local del sensor.',
      wifi_pending:'Wi-Fi --',server_pending:'Servidor --',buffer_pending:'Buffer --',send_pending:'Último envío --',
      force_read:'+ Hacer nueva lectura',reading_sensors:'Leyendo sensores...',
      metric_temp:'Temperatura',metric_humidity:'Humedad del aire',metric_tds:'Sólidos disueltos',
      metric_ph:'Potencial hidrog.',metric_water:'Nivel del agua',metric_battery:'Batería',
      no_reading:'sin lectura',manual_input:'entrada manual',servers_label:'S E R V I D O R E S',
      external_label:'E X T E R N O S',local_label:'L O C A L E S',system_label:'S I S T E M A',
      config_label:'C O N F I G U R A C I Ó N',config_heading:'Destinos y Sistema',sensor_name:'Nombre del sensor',
      public_servers:'Servidores públicos',local_server:'Servidor local',other_btn:'+ Otro',use_arateki:'Usar Arateki',
      save:'Guardar',tools:'Herramientas',status_api:'Status API',json:'JSON',reconnect_wifi:'Reconectar Wi-Fi',
      reconnect_confirm:'¿Reconectar Wi-Fi?',danger_zone:'Zona de peligro',factory_reset:'Reset completo (borrar claves)',
      name_placeholder:'Nombre',url_or_ip_port_placeholder:'URL o IP:puerto',ip_port_placeholder:'IP:puerto',url_placeholder:'URL',
      manual_label:'M A N U A L',docs_title:'Guía SafraSense',
      docs_subtitle:'Referencia rápida para configuración, monitoreo y cultivo hidropónico.',copy_docs_title:'Copiar manual completo',
      raiznet_label:'R E D   D E S C E N T R A L I Z A D A',raiznet_heading:'Estado Raiznet',
      connected_servers:'Servidores conectados',loading_status:'Cargando estado...',
      wifi_connected:'Wi-Fi conectado',wifi_offline:'Wi-Fi offline',server_online:'Servidor online',
      server_offline:'Servidor offline',pending_suffix:' pendiente(s)',last_send:'Último envío ',local_no_response:'Sin respuesta local',
      sensor_offline:'sensor offline',dht_active:'DHT activo',tds_active:'TDS activo',laser_active:'láser activo',
      public_key:'Clave pública',copy:'Copiar',copy_public_key:'Copiar clave pública',
      uptime:'Uptime',free_heap:'Heap libre',minutes_suffix:' min',kb_suffix:' KB',
      no_servers:'Ningún servidor configurado',server_default:'Servidor',online:'online',offline:'offline',
      ph_prompt:'Escriba el valor de pH (0-14):',invalid_ph_alert:'Valor inválido. Ingrese un número entre 0 y 14.',
      show_details:'Mostrar detalles: ',
      help_temp_title:'Temperatura',help_temp_text:'Indica el calor alrededor de la planta. Temperaturas fuera del rango ideal reducen el crecimiento, la absorción de agua y la respuesta a los nutrientes.',help_temp_range:'Rango ideal general: 20 a 28 °C.',
      help_hum_title:'Humedad del aire',help_hum_text:'Muestra cuánta humedad hay en el aire. La humedad muy baja aumenta la pérdida de agua; la muy alta favorece hongos y dificulta la transpiración de la planta.',help_hum_range:'Rango ideal general: 50 a 70%.',
      help_ec_title:'Sólidos disueltos',help_ec_text:'Estima la cantidad de sales y nutrientes disueltos en el agua. Valores bajos indican poca nutrición; valores altos pueden causar estrés en las raíces.',help_ec_range:'Rango ideal general: 500 a 1200 ppm, según el cultivo.',
      help_ph_title:'Potencial hidrogeniónico',help_ph_text:'Mide la acidez o alcalinidad del agua. El pH correcto es crucial para que la planta pueda absorber los nutrientes presentes en la solución.',help_ph_range:'Rango ideal general: 5.5 a 6.5.',
      help_water_title:'Nivel del agua',help_water_text:'Muestra la altura disponible en el reservorio. Un nivel bajo puede secar raíces, detener la circulación o concentrar demasiado los nutrientes.',help_water_range:'Rango ideal: por encima del mínimo seguro del reservorio.',
      help_battery_title:'Batería',help_battery_text:'Indica la energía restante del dispositivo. Batería baja puede interrumpir lecturas y retrasar el envío de datos a los servidores.',help_battery_range:'Rango ideal: por encima de 40%.'
    },
    '3':{
      select_language:'言語を選択',toggle_theme:'テーマを切り替え',nav_main:'メインナビゲーション',
      title_dashboard:'SafraSense Aqua',title_settings:'設定',title_manual:'ガイド — SafraSense Aqua',title_raiznet:'Raiznet - SafraSense',
      nav_home:'ホーム',nav_raiznet:'Raiznet',nav_config:'設定',nav_docs:'ガイド',
      overview_label:'総 覧',dashboard_empty_summary:'最初のローカルセンサー読み取りを待機中。',
      wifi_pending:'Wi-Fi --',server_pending:'サーバー --',buffer_pending:'Buffer --',send_pending:'最終送信 --',
      force_read:'+ 新しい読み取りを実行',reading_sensors:'センサーを読み取り中...',
      metric_temp:'温度',metric_humidity:'空気湿度',metric_tds:'溶解固形物',
      metric_ph:'水素イオン指数',metric_water:'水位',metric_battery:'バッテリー',
      no_reading:'読み取りなし',manual_input:'手動入力',servers_label:'サ ー バ ー',
      external_label:'外 部',local_label:'ロ ー カ ル',system_label:'シ ス テ ム',
      config_label:'設 定',config_heading:'送信先とシステム',sensor_name:'センサー名',
      public_servers:'公開サーバー',local_server:'ローカルサーバー',other_btn:'+ その他',use_arateki:'Aratekiを使用',
      save:'保存',tools:'工具',status_api:'Status API',json:'JSON',reconnect_wifi:'Wi-Fiに再接続',
      reconnect_confirm:'Wi-Fiに再接続しますか？',danger_zone:'危険ゾーン',factory_reset:'フルリセット（キーの消去）',
      name_placeholder:'名前',url_or_ip_port_placeholder:'URL または IP:ポート',ip_port_placeholder:'IP:ポート',url_placeholder:'URL',
      manual_label:'ガ イ ド',docs_title:'SafraSense ガイド',
      docs_subtitle:'設定、監視、水耕栽培のクイックリファレンス。',copy_docs_title:'ガイド全文をコピー',
      raiznet_label:'分 散 型 ネ ッ ト ワ ー ク',raiznet_heading:'Raiznetステータス',
      connected_servers:'接続済みサーバー',loading_status:'ステータスを読み込み中...',
      wifi_connected:'Wi-Fi接続済み',wifi_offline:'Wi-Fiオフライン',server_online:'サーバーオンライン',
      server_offline:'サーバーオフライン',pending_suffix:' 保留中',last_send:'最終送信 ',local_no_response:'ローカルレスポンスなし',
      sensor_offline:'センサーオフライン',dht_active:'DHT有効',tds_active:'TDS有効',laser_active:'レーザー有効',
      public_key:'公開キー',copy:'コピー',copy_public_key:'公開キーをコピー',
      uptime:'稼働時間',free_heap:'空きヒープ',minutes_suffix:' 分',kb_suffix:' KB',
      no_servers:'サーバーが設定されていません',server_default:'サーバー',online:'オンライン',offline:'オフライン',
      ph_prompt:'pH値を入力してください (0-14):',invalid_ph_alert:'無効な値です。0から14の間の数値を入力してください。',
      show_details:'詳細を表示: ',
      help_temp_title:'温度',help_temp_text:'植物周辺の熱を示します。理想的な範囲外の温度は、成長、吸水、養分への反応を低下させます。',help_temp_range:'一般的な理想範囲：20〜28°C。',
      help_hum_title:'空気湿度',help_hum_text:'空気中の水分量を示します。湿度が低すぎると水分の喪失が増え、高すぎるとカビを誘発し、植物の蒸散を困難にします。',help_hum_range:'一般的な理想範囲：50〜70%。',
      help_ec_title:'溶解固形物',help_ec_text:'水に溶けている塩分や養分の量を推定します。値が低いと栄養不足を示し、高いと根にストレスを与える可能性があります。',help_ec_range:'一般的な理想範囲：500〜1200 ppm（作物による）。',
      help_ph_title:'水素イオン指数',help_ph_text:'水の酸性度またはアルカリ性度を測定します。植物が溶液中の養分を吸収するためには、正しいpHが不可欠です。',help_ph_range:'一般的な理想範囲：5.5〜6.5。',
      help_water_title:'水位',help_water_text:'リザーバー内の利用可能な高さを示します。水位が低いと、根が乾燥したり、循環が止まったり、養分が過剰に濃縮されたりする可能性があります。',help_water_range:'理想的な範囲：リザーバーの安全最低水位以上。',
      help_battery_title:'バッテリー',help_battery_text:'デバイスの残電量を示します。バッテリー残量が少ないと、読み取りが中断されたり、サーバーへのデータ送信が遅れたりする可能性があります。',help_battery_range:'理想的な範囲：40%以上。'
    },
    '4':{
      select_language:'选择语言',toggle_theme:'切换主题',nav_main:'主导航',
      title_dashboard:'SafraSense Aqua',title_settings:'设置',title_manual:'手册 — SafraSense Aqua',title_raiznet:'Raiznet - SafraSense',
      nav_home:'首页',nav_raiznet:'Raiznet',nav_config:'设置',nav_docs:'手册',
      overview_label:'总览',dashboard_empty_summary:'正在等待第一次本地传感器读数。',
      wifi_pending:'Wi-Fi --',server_pending:'服务器 --',buffer_pending:'Buffer --',send_pending:'上次发送 --',
      force_read:'+ 立即读取',reading_sensors:'正在读取传感器...',
      metric_temp:'温度',metric_humidity:'空气湿度',metric_tds:'溶解固体',
      metric_ph:'氢离子浓度',metric_water:'水位',metric_battery:'电池',
      no_reading:'无读数',manual_input:'手动输入',servers_label:'服务器',
      external_label:'外部',local_label:'本地',system_label:'系统',
      config_label:'设置',config_heading:'目标与系统',sensor_name:'传感器名称',
      public_servers:'公共服务器',local_server:'本地服务器',other_btn:'+ 其他',use_arateki:'使用 Arateki',
      save:'保存',tools:'工具',status_api:'Status API',json:'JSON',reconnect_wifi:'重新连接 Wi-Fi',
      reconnect_confirm:'重新连接 Wi-Fi？',danger_zone:'危险区域',factory_reset:'完全重置（删除密钥）',
      name_placeholder:'名称',url_or_ip_port_placeholder:'URL 或 IP:端口',ip_port_placeholder:'IP:端口',url_placeholder:'URL',
      manual_label:'手册',docs_title:'SafraSense 指南',
      docs_subtitle:'配置、监测和水培种植的快速参考。',copy_docs_title:'复制完整手册',
      raiznet_label:'去中心化网络',raiznet_heading:'Raiznet 状态',
      connected_servers:'已连接服务器',loading_status:'正在加载状态...',
      wifi_connected:'Wi-Fi 已连接',wifi_offline:'Wi-Fi 离线',server_online:'服务器在线',
      server_offline:'服务器离线',pending_suffix:' 待处理',last_send:'上次发送 ',local_no_response:'无本地响应',
      sensor_offline:'传感器离线',dht_active:'DHT 正常',tds_active:'TDS 正常',laser_active:'激光正常',
      public_key:'公钥',copy:'复制',copy_public_key:'复制公钥',
      uptime:'运行时间',free_heap:'可用堆内存',minutes_suffix:' 分钟',kb_suffix:' KB',
      no_servers:'未配置服务器',server_default:'服务器',online:'在线',offline:'离线',
      ph_prompt:'输入 pH 值 (0-14):',invalid_ph_alert:'数值无效。请输入 0 到 14 之间的数字。',
      show_details:'显示详情：',
      help_temp_title:'温度',help_temp_text:'显示植物周围的热量。温度超出理想范围会降低生长、吸水和对养分的反应。',help_temp_range:'一般理想范围：20 到 28 °C。',
      help_hum_title:'空气湿度',help_hum_text:'显示空气中的水分含量。湿度过低会增加失水；湿度过高会促进真菌并让植物蒸腾更困难。',help_hum_range:'一般理想范围：50 到 70%。',
      help_ec_title:'溶解固体',help_ec_text:'估算水中溶解的盐分和养分数量。数值低表示营养不足；数值高可能使根系受压。',help_ec_range:'一般理想范围：500 到 1200 ppm，取决于作物。',
      help_ph_title:'氢离子浓度',help_ph_text:'测量水的酸碱度。正确的 pH 对植物吸收溶液中的养分至关重要。',help_ph_range:'一般理想范围：5.5 到 6.5。',
      help_water_title:'水位',help_water_text:'显示储液槽中的可用高度。水位过低可能让根系干燥、停止循环或让养分过度浓缩。',help_water_range:'理想范围：高于储液槽安全最低线。',
      help_battery_title:'电池',help_battery_text:'显示设备剩余电量。电量低可能中断读数并延迟数据发送到服务器。',help_battery_range:'理想范围：高于 40%。'
    }
  };
  function normalizeLang(value){
    if(value==='pt')return'1';
    if(value==='en')return'0';
    if(value==='es')return'2';
    if(value==='ja')return'3';
    if(value==='zh'||value==='zh-cn'||value==='zh-tw')return'4';
    return value==='1'||value==='2'||value==='3'||value==='4'?value:'0';
  }
  function systemLang(){
    var langs=(navigator.languages&&navigator.languages.length)?navigator.languages:[navigator.language||navigator.userLanguage||''];
    for(var i=0;i<langs.length;i++){
      var v=String(langs[i]||'').toLowerCase();
      if(v==='pt'||v.indexOf('pt-')===0)return'1';
      if(v==='en'||v.indexOf('en-')===0)return'0';
      if(v==='es'||v.indexOf('es-')===0)return'2';
      if(v==='ja'||v.indexOf('ja-')===0)return'3';
      if(v==='zh'||v.indexOf('zh-')===0)return'4';
    }
    return'0';
  }
  function readPref(key,fallback){
    try{var stored=localStorage.getItem(key);if(stored)return stored;}catch(_){}
    return fallback;
  }
  function writePref(key,value){
    try{localStorage.setItem(key,value);}catch(_){}
  }
  var queryLang='';
  try{queryLang=new URLSearchParams(location.search).get('lang')||'';}catch(_){}
  var activeLang=normalizeLang(queryLang||readPref('lang',systemLang()));
  if(queryLang)writePref('lang',activeLang);
  function textFor(lang){return Object.assign({},dict['0'],dict[lang]||{});}
  window.localLang=activeLang;
  window.localText=function(key,fallback){
    var value=textFor(window.localLang)[key];
    return value!==undefined?value:(fallback!==undefined?fallback:key);
  };
  window.localCurrentLanguage=function(){return window.localLang;};
  window.localSetLanguage=function(lang){
    var next=normalizeLang(lang);
    writePref('lang',next);
    var url=new URL(location.href);
    url.searchParams.set('lang',next);
    startLoading();
    setTimeout(function(){
      location.href=url.pathname+url.search+url.hash;
    },60);
  };
  function applyTranslations(){
    var t=textFor(window.localLang);
    document.documentElement.lang=window.localLang==='1'?'pt-BR':(window.localLang==='2'?'es':(window.localLang==='3'?'ja':(window.localLang==='4'?'zh-CN':'en')));
    if(location.pathname==='/')document.title=t.title_dashboard;
    else if(location.pathname==='/config')document.title=t.title_settings;
    else if(location.pathname==='/docs')document.title=t.title_manual;
    else if(location.pathname==='/raiznet')document.title=t.title_raiznet;
    document.querySelectorAll('[data-i18n]').forEach(function(el){
      var key=el.getAttribute('data-i18n');
      if(t[key]!==undefined)el.textContent=t[key];
    });
    document.querySelectorAll('[data-i18n-html]').forEach(function(el){
      var key=el.getAttribute('data-i18n-html');
      if(t[key]!==undefined)el.innerHTML=t[key];
    });
    document.querySelectorAll('[data-i18n-placeholder]').forEach(function(el){
      var key=el.getAttribute('data-i18n-placeholder');
      if(t[key]!==undefined)el.setAttribute('placeholder',t[key]);
    });
    document.querySelectorAll('[data-i18n-title]').forEach(function(el){
      var key=el.getAttribute('data-i18n-title');
      if(t[key]!==undefined)el.setAttribute('title',t[key]);
    });
    document.querySelectorAll('[data-i18n-aria-label]').forEach(function(el){
      var key=el.getAttribute('data-i18n-aria-label');
      if(t[key]!==undefined)el.setAttribute('aria-label',t[key]);
    });
    document.querySelectorAll('.lang-select').forEach(function(sel){
      sel.value=window.localLang;
      sel.setAttribute('aria-label',t.select_language);
      sel.onchange=function(){window.localSetLanguage(sel.value);};
    });
  }
  if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',applyTranslations,{once:true});
  else applyTranslations();
  function ensureLoader(){
    var loader=document.getElementById('loader-overlay');
    if(!loader){
      loader=document.createElement('div');
      loader.id='loader-overlay';
      document.body.appendChild(loader);
    }
    return loader;
  }
  function startLoading(){
    ensureLoader();
    document.body.classList.add('is-loading');
  }
  function stopLoading(){
    document.body.classList.remove('is-loading');
  }
  function shouldLoadLink(a,e){
    if(!a||e.defaultPrevented||e.button!==0||e.metaKey||e.ctrlKey||e.shiftKey||e.altKey)return false;
    if(a.target&&a.target!=='_self')return false;
    if(a.hasAttribute('download'))return false;
    var href=a.getAttribute('href');
    if(!href||href.charAt(0)==='#')return false;
    var url;
    try{url=new URL(href,location.href);}catch(_){return false;}
    if(url.origin!==location.origin)return false;
    if(url.pathname===location.pathname&&url.search===location.search)return false;
    return true;
  }
  function withLocalLang(href){
    var url=new URL(href,location.href);
    url.searchParams.set('lang',window.localLang||normalizeLang(readPref('lang',systemLang())));
    return url.pathname+url.search+url.hash;
  }
  if(document.readyState==='loading'){
    document.addEventListener('DOMContentLoaded',ensureLoader,{once:true});
  }else{
    ensureLoader();
  }
  document.addEventListener('click',function(e){
    var a=e.target.closest&&e.target.closest('a[href]');
    if(!shouldLoadLink(a,e))return;
    e.preventDefault();
    startLoading();
    var href=withLocalLang(a.href);
    setTimeout(function(){location.href=href;},60);
  });
  document.addEventListener('submit',function(e){
    var f=e.target;
    if(e.defaultPrevented||!f||f.target&&f.target!=='_self')return;
    try{
      var url=new URL(f.getAttribute('action')||location.href,location.href);
      if(url.origin===location.origin){
        url.searchParams.set('lang',window.localLang||normalizeLang(readPref('lang',systemLang())));
        f.action=url.pathname+url.search+url.hash;
      }
    }catch(_){}
    setTimeout(startLoading,0);
  });
  window.addEventListener('pageshow',stopLoading);
})();
)rawliteral";

const char LOCAL_DASHBOARD_JS[] PROGMEM = R"rawliteral(
(function(){
  const $ = (id) => document.getElementById(id);
  const doc = document.documentElement;
  const tr = (key, fallback) => window.localText ? window.localText(key, fallback) : fallback;
  window.copyId = function(text, btn) {
    if (navigator.clipboard && window.isSecureContext) {
      navigator.clipboard.writeText(text).catch(()=>{});
    } else {
      let ta = document.createElement("textarea");
      ta.value = text;
      ta.style.position = "fixed";
      ta.style.opacity = "0";
      document.body.appendChild(ta);
      ta.focus();
      ta.select();
      try { document.execCommand('copy'); } catch(e) {}
      document.body.removeChild(ta);
    }
    if (btn) {
      btn.classList.add('copied');
      const orig = btn.innerHTML;
      btn.innerHTML = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>';
      setTimeout(()=>{btn.classList.remove('copied');btn.innerHTML=orig;}, 1500);
    }
  };
  const moonSvg = "<svg viewBox='0 0 24 24' width='20' height='20' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' aria-hidden='true'><path d='M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z'/></svg>";
  const sunSvg = "<svg viewBox='0 0 24 24' width='20' height='20' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' aria-hidden='true'><circle cx='12' cy='12' r='4'/><line x1='12' y1='2' x2='12' y2='6'/><line x1='12' y1='18' x2='12' y2='22'/><line x1='4.93' y1='4.93' x2='7.76' y2='7.76'/><line x1='16.24' y1='16.24' x2='19.07' y2='19.07'/><line x1='2' y1='12' x2='6' y2='12'/><line x1='18' y1='12' x2='22' y2='12'/><line x1='4.93' y1='19.07' x2='7.76' y2='16.24'/><line x1='16.24' y1='7.76' x2='19.07' y2='4.93'/></svg>";
  const setThemeIcon = () => {
    const btn = $('themeBtn');
    if (btn) btn.innerHTML = doc.getAttribute('data-theme') === 'dark' ? sunSvg : moonSvg;
  };
  const storedTheme = localStorage.getItem('theme') || (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light');
  doc.setAttribute('data-theme', storedTheme);
  setThemeIcon();
  $('themeBtn').onclick = () => {
    const next = doc.getAttribute('data-theme') === 'dark' ? 'light' : 'dark';
    doc.setAttribute('data-theme', next);
    localStorage.setItem('theme', next);
    setThemeIcon();
  };

  function text(id, value) { const el = $(id); if (el) el.textContent = value; }
  function shortPublicKey(value) {
    if (!value || value === '--' || value.length <= 24) return value || '--';
    return value.slice(0, 10) + '...' + value.slice(-8);
  }
  function fmt(value, digits) {
    return value === null || value === undefined || Number.isNaN(value) ? '--' : Number(value).toFixed(digits);
  }
  function statusName(state) { return state === 'ok' ? 'ok' : state === 'warn' ? 'warn' : state === 'bad' ? 'bad' : ''; }
  function setPill(id, label, state) {
    const el = $(id);
    if (!el) return;
    el.className = 'status-pill ' + statusName(state);
    el.querySelector('span:last-child').textContent = label;
  }
  function metric(id, value, detail, state) {
    const el = $(id);
    if (!el) return;
    const open = el.classList.contains('is-help-open');
    el.className = 'metric-card ' + (state ? 'is-' + state : '');
    if (open) el.classList.add('is-help-open');
    el.querySelector('[data-value]').textContent = value;
    el.querySelector('[data-detail]').textContent = detail;
    el.className = 'metric-card' + (state ? ' is-' + state : '');
  }
  window.manualPh = function() {
    const current = document.querySelector('#mPh [data-value]').textContent;
    const val = prompt(tr('ph_prompt', 'Digite o valor do PH (0-14):'), current === '--' ? '7.0' : current);
    if (val !== null) {
      const ph = parseFloat(val.replace(',', '.'));
      if (!isNaN(ph) && ph >= 0 && ph <= 14) {
        fetch('/api/ph/manual?ph=' + ph, { method: 'POST' }).then(() => refresh());
      } else {
        alert(tr('invalid_ph_alert', 'Valor inválido. Insira um número entre 0 e 14.'));
      }
    }
  };
  window.forceRead = function() {
    const btn = $('forceReadBtn');
    btn.disabled = true;
    btn.textContent = tr('reading_sensors', 'Lendo sensores...');
    fetch('/api/force-read').then(() => {
      setTimeout(() => {
        refresh().then(() => {
          btn.disabled = false;
          btn.textContent = tr('force_read', '+ Fazer nova leitura');
        });
      }, 2000);
    });
  };
  const metricHelp = {
    mTemp: {
      title: tr('help_temp_title', 'Temperatura'),
      text: tr('help_temp_text', 'Indica o calor do ambiente ao redor da planta. Temperaturas fora da faixa ideal reduzem crescimento, absorção de água e resposta aos nutrientes.'),
      range: tr('help_temp_range', 'Faixa ideal geral: 20 a 28 °C.')
    },
    mHum: {
      title: tr('help_hum_title', 'Umidade do ar'),
      text: tr('help_hum_text', 'Mostra quanta umidade existe no ar. Umidade muito baixa aumenta perda de água; muito alta favorece fungos e dificulta a transpiração da planta.'),
      range: tr('help_hum_range', 'Faixa ideal geral: 50 a 70%.')
    },
    mEc: {
      title: tr('help_ec_title', 'Sólidos dissolvidos'),
      text: tr('help_ec_text', 'Estima a quantidade de sais e nutrientes dissolvidos na água. Valores baixos indicam pouca nutrição; valores altos podem causar estresse nas raízes.'),
      range: tr('help_ec_range', 'Faixa ideal geral: 500 a 1200 ppm, conforme a cultura.')
    },
    mPh: {
      title: tr('help_ph_title', 'Potencial Hidrogeniônico'),
      text: tr('help_ph_text', 'Mede a acidez ou alcalinidade da água. O pH correto é crucial para que a planta consiga absorver os nutrientes presentes na solução.'),
      range: tr('help_ph_range', 'Faixa ideal geral: 5.5 a 6.5.')
    },
    mWater: {
      title: tr('help_water_title', 'Nível da água'),
      text: tr('help_water_text', 'Mostra a altura disponível no reservatório. Nível baixo pode secar raízes, parar circulação ou concentrar demais os nutrientes.'),
      range: tr('help_water_range', 'Faixa ideal: acima do mínimo seguro do reservatório.')
    },
    mBattery: {
      title: tr('help_battery_title', 'Bateria'),
      text: tr('help_battery_text', 'Indica a energia restante do dispositivo. Bateria baixa pode interromper leituras e atrasar o envio dos dados para os servidores.'),
      range: tr('help_battery_range', 'Faixa ideal: acima de 40%.')
    }
  };
  function setupMetricHelp() {
    Object.keys(metricHelp).forEach((id) => {
      const card = $(id);
      const info = metricHelp[id];
      if (!card || card.querySelector('.metric-help')) return;
      card.tabIndex = 0;
      card.setAttribute('role', 'button');
      card.setAttribute('aria-expanded', 'false');
      card.setAttribute('aria-label', tr('show_details', 'Mostrar detalhes: ') + info.title);
      const help = document.createElement('div');
      help.className = 'metric-help';
      help.innerHTML = '<strong>' + info.title + '</strong><span>' + info.text + '</span><span class="metric-range">' + info.range + '</span>';
      card.appendChild(help);
      const toggle = () => {
        const next = !card.classList.contains('is-help-open');
        document.querySelectorAll('.metric-card.is-help-open').forEach((openCard) => {
          if (openCard !== card) {
            openCard.classList.remove('is-help-open');
            openCard.setAttribute('aria-expanded', 'false');
          }
        });
        card.classList.toggle('is-help-open', next);
        card.setAttribute('aria-expanded', next ? 'true' : 'false');
      };
      card.addEventListener('click', toggle);
      card.addEventListener('keydown', (event) => {
        if (event.key === 'Enter' || event.key === ' ') {
          event.preventDefault();
          toggle();
        }
      });
    });
  }
  function infoRow(label, value) {
    const el = document.createElement('div');
    el.className = 'info-row';
    const a = document.createElement('span');
    const b = document.createElement('span');
    a.textContent = label;
    b.innerHTML = value || '--';
    el.appendChild(a);
    el.appendChild(b);
    return el;
  }
  function renderServers(id, list) {
    const root = $(id);
    if (!root) return;
    root.textContent = '';
    if (!list || !list.length) {
      const empty = document.createElement('div');
      empty.className = 'empty';
      empty.textContent = tr('no_servers', 'Nenhum servidor configurado');
      root.appendChild(empty);
      return;
    }
    list.forEach((item) => {
      const chip = document.createElement('div');
      const online = item.online === true;
      chip.className = 'server-chip';
      const top = document.createElement('div');
      top.className = 'server-chip-top';
      const name = document.createElement('strong');
      const status = document.createElement('div');
      const url = document.createElement('span');
      name.textContent = item.name || tr('server_default', 'Servidor');
      status.className = 'server-status ' + (online ? 'ok' : 'bad');
      status.textContent = online ? tr('online', 'online') : tr('offline', 'offline');
      url.textContent = item.url || '--';
      top.appendChild(name);
      top.appendChild(status);
      chip.appendChild(top);
      chip.appendChild(url);
      root.appendChild(chip);
    });
  }
  function sensorState(ok) { return ok === false ? 'bad' : 'ok'; }
  function batteryState(percent) {
    if (percent === null || percent === undefined) return '';
    return percent < 20 ? 'bad' : percent < 40 ? 'warn' : 'ok';
  }
  async function refresh() {
    try {
      const response = await fetch('/api/status');
      const d = await response.json();
      const r = d.readings || {};
      const s = d.sensors || {};
      text('deviceName', d.device_name || 'SafraSense Aqua');
      const did = d.device_id || '--';
      const didShort = shortPublicKey(did);
      const copyBtn = did !== '--' ? `<button type="button" class="copy-btn" onclick="window.copyId('${did}', this)" aria-label="${tr('copy', 'Copiar')}" title="${tr('copy_public_key', 'Copiar chave pública')}"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg></button>` : '';
      const summary = $('deviceSummary');
      if (summary) summary.innerHTML = `<span class="dev-net">${d.ip || '--'} &middot; ${d.mdns || 'safrasense'}.local</span> <span class="dev-sep">&middot;</span> <span class="dev-key">${tr('public_key', 'Chave pública')}: <span class="mono" title="${did}">${didShort}</span> ${copyBtn}</span>`;
      setPill('wifiPill', d.wifi_ok ? tr('wifi_connected', 'Wi-Fi conectado') : tr('wifi_offline', 'Wi-Fi offline'), d.wifi_ok ? 'ok' : 'bad');
      setPill('serverPill', d.server_ok ? tr('server_online', 'Servidor online') : tr('server_offline', 'Servidor offline'), d.server_ok ? 'ok' : 'bad');
      setPill('bufferPill', (d.buffer_pending || 0) + tr('pending_suffix', ' pendente(s)'), d.buffer_pending > 0 ? 'warn' : 'ok');
      setPill('sendPill', tr('last_send', 'Último envio ') + (d.last_send_time || '--'), d.server_ok ? 'ok' : 'warn');

      metric('mTemp', fmt(r.temp_ambient, 1), s.dht === false ? tr('sensor_offline', 'sensor offline') : tr('dht_active', 'DHT ativo'), sensorState(s.dht));
      metric('mHum', fmt(r.humidity, 1), s.dht === false ? tr('sensor_offline', 'sensor offline') : tr('dht_active', 'DHT ativo'), sensorState(s.dht));
      metric('mEc', r.ec === undefined ? '--' : Math.round(r.ec), s.tds === false ? tr('sensor_offline', 'sensor offline') : tr('tds_active', 'TDS ativo'), sensorState(s.tds));
      metric('mPh', r.ph === undefined ? '--' : fmt(r.ph, 1), tr('manual_input', 'entrada manual'), 'ok');
      metric('mWater', r.water_level === undefined ? '--' : fmt(r.water_level / 10, 1), s.laser === false ? tr('sensor_offline', 'sensor offline') : tr('laser_active', 'laser ativo'), sensorState(s.laser));
      metric('mBattery', r.bat_percent === undefined ? '--' : Math.round(r.bat_percent), r.bat_volts === undefined ? tr('no_reading', 'sem leitura') : fmt(r.bat_volts, 2) + ' V', batteryState(r.bat_percent));

      const info = $('systemInfo');
      if (info) {
        info.textContent = '';
        const uptimeMin = Math.floor((d.uptime_s || 0) / 60);
        [
          ['IP', d.ip],
          ['mDNS', (d.mdns || '--') + '.local'],
          ['MAC', d.mac],
          [tr('uptime', 'Uptime'), uptimeMin + tr('minutes_suffix', ' min')],
          [tr('free_heap', 'Heap livre'), Math.floor((d.free_heap || 0) / 1024) + tr('kb_suffix', ' KB')],
          [tr('public_key', 'Chave p&uacute;blica'), '<span class="mono" title="' + did + '">' + didShort + '</span> ' + copyBtn]
        ].forEach((row) => info.appendChild(infoRow(row[0], row[1])));
      }

      renderServers('externalServers', d.servers_external);
      renderServers('localServers', d.servers_local);
    } catch (err) {
      setPill('wifiPill', tr('local_no_response', 'Sem resposta local'), 'bad');
    }
    setTimeout(refresh, 5000);
  }
  setupMetricHelp();
  refresh();
})();
)rawliteral";

// ── /api/status ───────────────────────────────────────────────────────────

static void handleApiStatus() {
  TelemetryState ts = getTelemetryState();
  JsonDocument   doc;
  bool wifiOk = (WiFi.status() == WL_CONNECTED);

  doc["device_name"]    = gCfg->device_name;
  doc["device_id"]      = gId->public_key_hex;
  doc["mac"]            = gId->mac;
  doc["mdns"]           = getMdnsName();
  doc["ip"]             = WiFi.localIP().toString();
  doc["wifi_ok"]        = wifiOk;
  doc["server_ok"]      = ts.last_send_ok;
  doc["last_send_time"] = ts.last_send_time;
  doc["fail_streak"]    = ts.fail_streak;
  doc["buffer_pending"] = pendingCount();
  doc["buffer_total"]   = bufferTotal();
  doc["uptime_s"]       = millis() / 1000;
  doc["free_heap"]      = ESP.getFreeHeap();

  JsonObject sens = doc["sensors"].to<JsonObject>();
  sens["dht"]     = gLastReading.status.dht_ok;
  sens["tds"]     = gLastReading.status.tds_ok;
  sens["laser"]   = gLastReading.status.laser_ok;
  sens["battery"] = gLastReading.status.battery_ok;

  JsonObject rdg = doc["readings"].to<JsonObject>();
  if (gHasReading) {
    if (!isnan(gLastReading.temp_ambient)) rdg["temp_ambient"] = gLastReading.temp_ambient;
    if (!isnan(gLastReading.humidity))     rdg["humidity"]     = gLastReading.humidity;
    if (!isnan(gLastReading.ec))           rdg["ec"]           = gLastReading.ec;
    if (!isnan(gLastReading.ph))           rdg["ph"]           = gLastReading.ph;
    if (gLastReading.water_level >= 0)     rdg["water_level"]  = gLastReading.water_level;
    rdg["bat_volts"]   = gLastReading.bat_volts;
    rdg["bat_percent"] = gLastReading.bat_percent;
  }

  // Server list with confirmation status.
  JsonArray ext = doc["servers_external"].to<JsonArray>();
  for (size_t i = 0; i < gCfg->servers_external.size() && i < 16; i++) {
    const auto& s = gCfg->servers_external[i];
    JsonObject o = ext.add<JsonObject>();
    o["name"]   = s.name;
    o["url"]    = s.url;
    o["online"] = wifiOk && ((ts.online_mask & (1u << i)) != 0);
  }
  JsonArray loc = doc["servers_local"].to<JsonArray>();
  for (size_t i = 0; i < gCfg->servers_local.size() && i < 16; i++) {
    const auto& s = gCfg->servers_local[i];
    uint8_t bit = (uint8_t)(16 + i);
    JsonObject o = loc.add<JsonObject>();
    o["name"]   = s.name;
    o["url"]    = s.url;
    o["online"] = wifiOk && ((ts.online_mask & (1u << bit)) != 0);
  }

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

// ── /api/telemetry ────────────────────────────────────────────────────────

static void handleApiTelemetry() {
  if (!gHasReading) {
    Language lang = currentLocalLanguage();
    String body = "{\"error\":\"";
    body += localChoice(lang, "no readings yet", "sem leituras ainda", "sin lecturas todavía", "まだ読み取りがありません", "还没有读数");
    body += "\"}";
    server.send(503, "application/json", body);
    return;
  }
  JsonDocument doc;
  doc["device_id"]  = gId->public_key_hex;
  doc["device_mac"] = gId->mac;
  doc["timestamp"]  = gLastReading.captured_at;

  JsonObject r = doc["readings"].to<JsonObject>();
  JsonObject s = doc["sensor_status"].to<JsonObject>();
  if (!isnan(gLastReading.temp_ambient)) r["temp_ambient"] = gLastReading.temp_ambient;
  if (!isnan(gLastReading.humidity))     r["humidity"]     = gLastReading.humidity;
  if (!isnan(gLastReading.ec))           r["ec"]           = gLastReading.ec;
  if (gLastReading.water_level >= 0)     r["water_level"]  = gLastReading.water_level;
  r["bat_volts"]   = gLastReading.bat_volts;
  r["bat_percent"] = gLastReading.bat_percent;
  s["dht"]     = gLastReading.status.dht_ok;
  s["tds"]     = gLastReading.status.tds_ok;
  s["laser"]   = gLastReading.status.laser_ok;
  s["battery"] = gLastReading.status.battery_ok;

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

// ── / (dashboard) ─────────────────────────────────────────────────────────

static void handleRoot() {
  Language lang = currentLocalLanguage();
  String html = FPSTR(LOCAL_DASHBOARD_HTML);
  localizeLocalHtml(html, lang, "title_dashboard");
  html.replace("class=\"local-tab\" href=\"/\"", "class=\"local-tab is-active\" href=\"/\"");
  if (!gCfg->servers_external.empty()) {
    html.replace("id=\"raiznet-menu-item\" href=\"/raiznet\" style=\"display:none\"", "id=\"raiznet-menu-item\" href=\"/raiznet\"");
  }
  server.send(200, "text/html", html);
}

static void handleLocalCss() {
  server.send_P(200, PSTR("text/css"), LOCAL_PORTAL_CSS, strlen_P(LOCAL_PORTAL_CSS));
}

static void handleDashboardJs() {
  server.send_P(200, PSTR("application/javascript"), LOCAL_DASHBOARD_JS, strlen_P(LOCAL_DASHBOARD_JS));
}

static void handleLocalNavJs() {
  server.send_P(200, PSTR("application/javascript"), LOCAL_NAV_JS, strlen_P(LOCAL_NAV_JS));
}

// ── /config (GET) ─────────────────────────────────────────────────────────

static String serverRows(const std::vector<ServerEntry>& list, const char* prefix) {
  String html;
  for (size_t i = 0; i < list.size(); i++) {
    html += "<div class='srow' id='" + String(prefix) + String(i) + "'>";
    html += "<input type='text' class='form-input' name='" + String(prefix) + "_name_" + String(i) + "' placeholder='Nome' data-i18n-placeholder='name_placeholder' value='" + list[i].name + "'>";
    html += "<input type='text' class='form-input' name='" + String(prefix) + "_url_"  + String(i) + "' placeholder='URL ou IP:porta' data-i18n-placeholder='url_or_ip_port_placeholder' value='" + list[i].url + "'>";
    html += "<button type='button' class='btn btn-danger' style='padding:11px 14px;' onclick='removeRow(this)'>✕</button></div>";
  }
  return html;
}

static void handleConfig() {
  Language lang = currentLocalLanguage();
  String extRows = serverRows(gCfg->servers_external, "ext");
  String locRows = serverRows(gCfg->servers_local,    "loc");

  String html = R"HTML(<!DOCTYPE html>
<html lang="pt-BR"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Configurações</title>
<script>(function(){try{var u=new URL(location.href);if(!u.searchParams.has('lang')){var l=localStorage.getItem('lang');if(l){l=String(l).toLowerCase();l=(l==='1'||l==='pt')?'1':((l==='2'||l==='es')?'2':((l==='3'||l==='ja')?'3':((l==='4'||l==='zh'||l.indexOf('zh-')===0)?'4':'0')));u.searchParams.set('lang',l);location.replace(u.pathname+u.search+u.hash);return;}}}catch(_){}var t='light';try{t=localStorage.getItem('theme')||(window.matchMedia&&window.matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light');}catch(_){}document.documentElement.setAttribute('data-theme',t);})();</script>
<link rel="stylesheet" href="/local.css">
</head><body>
<header class="local-header">
  <a class="local-brand" href="/">
    <span class="local-brand-title">S A F R A S E N S E <span class="brand-aqua">A Q U A</span></span>
  </a>
  <nav class="local-tabs" aria-label="Navegação principal" data-i18n-aria-label="nav_main">
    <a class="local-tab" href="/" data-i18n="nav_home">Início</a>
    <a class="local-tab" id="raiznet-menu-item" href="/raiznet" style="display:none" data-i18n="nav_raiznet">Raiznet</a>
    <a class="local-tab is-active" href="/config" data-i18n="nav_config">Configurações</a>
    <a class="local-tab" href="/docs" data-i18n="nav_docs">Manual</a>
  </nav>
  <div class="header-actions">
    <select class="lang-select" id="langSelect" aria-label="Selecionar idioma" data-i18n-aria-label="select_language">
      <option value="1">PT</option>
      <option value="0">EN</option>
      <option value="2">ES</option>
      <option value="3">JA</option>
      <option value="4">ZH</option>
    </select>
    <button class="theme-btn local-theme" id="themeBtn" type="button" aria-label="Alternar tema" data-i18n-aria-label="toggle_theme"></button>
  </div>
</header>
<div class="portal-shell">
  <main class="main">
    <div class="topbar">
      <div class="title">
        <div class="eyebrow" data-i18n="config_label">C O N F I G U R A Ç Õ E S</div>
        <h1 class="serif" data-i18n="config_heading">Destinos e Sistema</h1>
      </div>
    </div>
    <div class="content-grid">
      <div class="panel">
        <form method="POST" action="/config/save" id="f">
          <input type="hidden" id="ext_count" name="ext_count" value="0">
          <input type="hidden" id="loc_count" name="loc_count" value="0">

          <label class="form-label" data-i18n="sensor_name">Nome do sensor</label>
          <input type="text" class="form-input" name="device_name" value=")HTML";
  html += gCfg->device_name;
  html += R"HTML(" maxlength="32">

          <div class="eyebrow" style="color:var(--primary);margin:24px 0 10px" data-i18n="public_servers">Servidores Públicos</div>
          <div id="ext_list">)HTML";

  if (!gCfg->servers_external.empty()) {
    html.replace("id=\"raiznet-menu-item\" href=\"/raiznet\" style=\"display:none\"", "id=\"raiznet-menu-item\" href=\"/raiznet\"");
  }

  html += extRows;
  html += R"HTML(</div>
          <div style="display:flex;gap:8px;margin-bottom:10px">
            <button type="button" class="btn" onclick="addRow('ext')" data-i18n="other_btn">+ Outro</button>
            <button type="button" class="btn" id="ara-btn" onclick="addArateki()" data-i18n="use_arateki">Usar Arateki</button>
          </div>

          <div class="eyebrow" style="margin:24px 0 10px" data-i18n="local_server">Servidor Local</div>
          <div id="loc_list">)HTML";
  html += locRows;
  html += R"HTML(</div>
          <button type="button" class="btn" onclick="addRow('loc')" data-i18n="other_btn">+ Outro</button>

          <div style="margin-top:24px"><button type="submit" class="btn btn-primary" style="width:100%" data-i18n="save">Salvar</button></div>
        </form>
      </div>
      <div>
        <div class="panel">
          <div class="eyebrow" style="margin-bottom:14px" data-i18n="tools">Ferramentas</div>
          <div style="display:grid;grid-template-columns:1fr;gap:8px">
            <a href="/api/status" target="_blank" class="btn" style="text-align:center" data-i18n="status_api">Status API</a>
            <a href="/api/telemetry" target="_blank" class="btn" style="text-align:center" data-i18n="json">JSON</a>
            <a href="/reset/wifi" onclick="return confirm(window.localText ? window.localText('reconnect_confirm','Reconectar Wi-Fi?') : 'Reconectar Wi-Fi?')" class="btn" style="text-align:center" data-i18n="reconnect_wifi">Reconectar Wi-Fi</a>
          </div>
        </div>
        <div class="panel" style="margin-top:36px;border-top-color:var(--bad)">
          <div class="eyebrow" style="margin-bottom:14px;color:var(--bad)" data-i18n="danger_zone">Zona de perigo</div>
          <button class="btn btn-danger" style="width:100%" onclick="location='/reset/factory'" data-i18n="factory_reset">Reset Completo (Apagar Chaves)</button>
        </div>
      </div>
    </div>
  </main>
</div>
<script src="/local-nav.js"></script>
<script>
const tr=(key,fallback)=>window.localText?window.localText(key,fallback):fallback;
const tb=document.getElementById('themeBtn'), doc=document.documentElement;
const moonSvg="<svg viewBox='0 0 24 24' width='20' height='20' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' aria-hidden='true'><path d='M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z'/></svg>";
const sunSvg="<svg viewBox='0 0 24 24' width='20' height='20' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' aria-hidden='true'><circle cx='12' cy='12' r='4'/><line x1='12' y1='2' x2='12' y2='6'/><line x1='12' y1='18' x2='12' y2='22'/><line x1='4.93' y1='4.93' x2='7.76' y2='7.76'/><line x1='16.24' y1='16.24' x2='19.07' y2='19.07'/><line x1='2' y1='12' x2='6' y2='12'/><line x1='18' y1='12' x2='22' y2='12'/><line x1='4.93' y1='19.07' x2='7.76' y2='16.24'/><line x1='16.24' y1='7.76' x2='19.07' y2='4.93'/></svg>";
function setThemeIcon(){tb.innerHTML=doc.getAttribute('data-theme')==='dark'?sunSvg:moonSvg}
const cur=localStorage.getItem('theme')||(window.matchMedia&&window.matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light');
doc.setAttribute('data-theme',cur);
setThemeIcon();
tb.onclick=()=>{const n=doc.getAttribute('data-theme')==='dark'?'light':'dark';doc.setAttribute('data-theme',n);localStorage.setItem('theme',n);setThemeIcon();};

function countRows(pfx){return document.getElementById(pfx+'_list').querySelectorAll('.srow').length}
function updateCounts(){
  document.getElementById('ext_count').value=countRows('ext');
  document.getElementById('loc_count').value=countRows('loc');
  const btn=document.getElementById('ara-btn');
  if(!btn)return;
  let hasAra=false;
  document.getElementById('ext_list').querySelectorAll('.srow').forEach(row=>{
    const url=row.querySelector('input[name^="ext_url_"]').value;
    if(url===")HTML";
  html += DEFAULT_SERVER_EXT_URL;
  html += R"HTML(") hasAra=true;
  });
  btn.disabled=hasAra;
  btn.style.opacity=hasAra?0.5:1;
  btn.style.cursor=hasAra?'not-allowed':'pointer';
}
function addRow(pfx){
  const list=document.getElementById(pfx+'_list');
  const i=list.querySelectorAll('.srow').length;
  const d=document.createElement('div');d.className='srow';d.id=pfx+i;
  d.innerHTML=`<input type="text" class="form-input" name="${pfx}_name_${i}" placeholder="${tr('name_placeholder','Nome')}" data-i18n-placeholder="name_placeholder">
    <input type="text" class="form-input" name="${pfx}_url_${i}" placeholder="${pfx==='loc'?tr('ip_port_placeholder','IP:porta'):tr('url_placeholder','URL')}" data-i18n-placeholder="${pfx==='loc'?'ip_port_placeholder':'url_placeholder'}">
    <button type="button" class="btn btn-danger" style="padding:11px 14px" onclick="removeRow(this)">✕</button>`;
  list.appendChild(d);
  d.querySelector('input').focus();
  updateCounts();
}
function removeRow(btn){btn.closest('.srow').remove();updateCounts()}
function addArateki(){
  const list=document.getElementById('ext_list');
  const i=list.querySelectorAll('.srow').length;
  const d=document.createElement('div');d.className='srow';d.id='ext'+i;
  d.innerHTML=`<input type="text" class="form-input" name="ext_name_${i}" value=")HTML";
  html += DEFAULT_SERVER_EXT_NAME;
  html += R"HTML("><input type="text" class="form-input" name="ext_url_${i}" value=")HTML";
  html += DEFAULT_SERVER_EXT_URL;
  html += R"HTML("><button type="button" class="btn btn-danger" style="padding:11px 14px" onclick="removeRow(this)">✕</button>`;
  list.appendChild(d);updateCounts();
}
document.getElementById('f').addEventListener('submit',updateCounts);
document.getElementById('ext_list').addEventListener('input',updateCounts);
updateCounts();
</script></body></html>)HTML";

  localizeLocalHtml(html, lang, "title_settings");
  server.send(200, "text/html", html);
}

// ── /config/save (POST) ───────────────────────────────────────────────────

static void handleConfigSave() {
  gCfg->device_name = server.arg("device_name");

  int extCount = server.arg("ext_count").toInt();
  gCfg->servers_external.clear();
  for (int i = 0; i < extCount; i++) {
    String name = server.arg("ext_name_" + String(i));
    String url  = server.arg("ext_url_"  + String(i));
    if (name.length() > 0 && url.length() > 0) {
      gCfg->servers_external.push_back({ name, url });
    }
  }

  int locCount = server.arg("loc_count").toInt();
  gCfg->servers_local.clear();
  for (int i = 0; i < locCount; i++) {
    String name = server.arg("loc_name_" + String(i));
    String url  = server.arg("loc_url_"  + String(i));
    if (name.length() > 0 && url.length() > 0) {
      gCfg->servers_local.push_back({ name, url });
    }
  }

  saveConfig(*gCfg);
  clearTelemetryServerStatus();
  server.sendHeader("Location", String("/config?lang=") + currentLocalLangCode());
  server.send(302, "text/plain", "");
}

// ── /reset/wifi ───────────────────────────────────────────────────────────

static void handleResetWifi() {
  Language lang = currentLocalLanguage();
  String langCode = currentLocalLangCode();
  String html = "<!DOCTYPE html><html lang='" + String(localHtmlLang(lang)) + "'><body style='font-family:sans-serif;background:#0f1117;color:#e8e8e8;padding:20px'>";
  html += "<h2>";
  html += localText(lang, "reset_wifi_title");
  html += "...</h2><p>";
  html += localText(lang, "reset_wifi_body");
  html += "</p><script>setTimeout(()=>location='/?lang=";
  html += langCode;
  html += "',5000)</script></body></html>";
  server.send(200, "text/html", html);
  delay(200);
  WiFi.disconnect(false);
  delay(300);
  WiFi.begin();
}

// ── /reset/factory (GET - confirmation page) ──────────────────────────────

static void handleResetFactoryPage() {
  Language lang = currentLocalLanguage();
  String langCode = currentLocalLangCode();
  String html = R"HTML(<!DOCTYPE html>
<html lang=")HTML";
  html += localHtmlLang(lang);
  html += R"HTML("><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>)HTML";
  html += localText(lang, "factory_title");
  html += R"HTML(</title>
<script>(function(){try{var u=new URL(location.href);if(!u.searchParams.has('lang')){var l=localStorage.getItem('lang');if(l){l=String(l).toLowerCase();l=(l==='1'||l==='pt')?'1':((l==='2'||l==='es')?'2':((l==='3'||l==='ja')?'3':((l==='4'||l==='zh'||l.indexOf('zh-')===0)?'4':'0')));u.searchParams.set('lang',l);location.replace(u.pathname+u.search+u.hash);return;}}}catch(_){}var t='light';try{t=localStorage.getItem('theme')||(window.matchMedia&&window.matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light');}catch(_){}document.documentElement.setAttribute('data-theme',t);})();</script>
<style>
:root { --bg:#f4f1ea; --fg:#1d231e; --line:#d8d2bf; --bad:#a83a2a; }
[data-theme="dark"] { --bg:#0d1310; --fg:#d8e3d4; --line:#20281f; --bad:#d36e63; }
body{font-family:-apple-system,sans-serif;background:var(--bg);color:var(--fg);padding:20px;transition:background .2s}
a{color:var(--fg);text-decoration:none;font-size:12px;text-transform:uppercase;letter-spacing:.05em}
.box{border:1px solid var(--bad);border-radius:2px;padding:20px;margin-top:24px;background:color-mix(in srgb, var(--bad) 5%, transparent)}
h2{color:var(--bad);font-family:Georgia,serif;font-weight:normal;margin-bottom:12px}
p{font-size:13px;line-height:1.6;margin-bottom:12px}
input{width:100%;padding:12px;background:transparent;border:1px solid var(--line);border-radius:2px;color:var(--fg);font-family:monospace;margin-top:10px}
button{margin-top:16px;padding:14px;background:var(--bad);border:none;border-radius:2px;color:#fff;cursor:pointer;width:100%;opacity:.4;text-transform:uppercase;letter-spacing:.04em;font-weight:600}
button.active{opacity:1}
</style></head><body>
<a href="/config?lang=)HTML";
  html += langCode;
  html += R"HTML(">)HTML";
  html += localText(lang, "factory_back");
  html += R"HTML(</a>
<div class="box">
  <h2>)HTML";
  html += localText(lang, "factory_title");
  html += R"HTML(</h2>
  <p>)HTML";
  html += localText(lang, "factory_warning");
  html += R"HTML(</p>
  <p>)HTML";
  html += localText(lang, "factory_confirm_hint");
  html += R"HTML(</p>
  <input type="text" id="pin" oninput="check()" placeholder=")HTML";
  html += localText(lang, "factory_placeholder");
  html += R"HTML(">
  <form method="POST" action="/reset/factory/confirm?lang=)HTML";
  html += langCode;
  html += R"HTML(" id="f">
    <button type="submit" id="btn" disabled>)HTML";
  html += localText(lang, "factory_button");
  html += R"HTML(</button>
  </form>
</div>
<script>
document.documentElement.setAttribute('data-theme',localStorage.getItem('theme')||(window.matchMedia&&window.matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light'));
function check(){
  const ok=document.getElementById('pin').value===')HTML";
  html += localText(lang, "factory_placeholder");
  html += R"HTML(';
  document.getElementById('btn').disabled=!ok;
  document.getElementById('btn').className=ok?'active':'';
}
</script></body></html>)HTML";
  server.send(200, "text/html", html);
}

// ── /reset/factory/confirm (POST) ─────────────────────────────────────────

static void handleResetFactoryConfirm() {
  Language lang = currentLocalLanguage();
  String html = "<!DOCTYPE html><html lang='" + String(localHtmlLang(lang)) + "'><body style='font-family:sans-serif;background:#0f1117;color:#e8e8e8;padding:20px'>";
  html += "<h2>";
  html += localText(lang, "factory_running_title");
  html += "</h2><p>";
  html += localText(lang, "factory_running_body");
  html += "</p></body></html>";
  server.send(200, "text/html", html);
  delay(500);
  gPendingAction = ACTION_FACTORY_RESET;  // main.cpp executes it on the next loop
}

// ── /docs ─────────────────────────────────────────────────────────────────

static const char DOCS_HEADER_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Manual — SafraSense Aqua</title>
<script>(function(){try{var u=new URL(location.href);if(!u.searchParams.has('lang')){var l=localStorage.getItem('lang');if(l){l=String(l).toLowerCase();l=(l==='1'||l==='pt')?'1':((l==='2'||l==='es')?'2':((l==='3'||l==='ja')?'3':((l==='4'||l==='zh'||l.indexOf('zh-')===0)?'4':'0')));u.searchParams.set('lang',l);location.replace(u.pathname+u.search+u.hash);return;}}}catch(_){}var t='light';try{t=localStorage.getItem('theme')||(window.matchMedia&&window.matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light');}catch(_){}document.documentElement.setAttribute('data-theme',t);})();</script>
<link rel="stylesheet" href="/local.css">
</head>
<body>
<header class="local-header">
  <a class="local-brand" href="/">
    <span class="local-brand-title">S A F R A S E N S E <span class="brand-aqua">A Q U A</span></span>
  </a>
  <nav class="local-tabs" aria-label="Navegação principal" data-i18n-aria-label="nav_main">
    <a class="local-tab" href="/" data-i18n="nav_home">Início</a>
    <a class="local-tab" id="raiznet-menu-item" href="/raiznet" style="display:none" data-i18n="nav_raiznet">Raiznet</a>
    <a class="local-tab" href="/config" data-i18n="nav_config">Configurações</a>
    <a class="local-tab" href="/docs" data-i18n="nav_docs">Manual</a>
  </nav>
  <div class="header-actions">
    <select class="lang-select" id="langSelect" aria-label="Selecionar idioma" data-i18n-aria-label="select_language">
      <option value="1">PT</option>
      <option value="0">EN</option>
      <option value="2">ES</option>
      <option value="3">JA</option>
      <option value="4">ZH</option>
    </select>
    <button class="theme-btn local-theme" id="themeBtn" type="button" aria-label="Alternar tema" data-i18n-aria-label="toggle_theme"></button>
  </div>
</header>
<div class="portal-shell">
<main class="main doc-wrap">
<div class="topbar">
  <div class="title">
    <div class="eyebrow" data-i18n="manual_label">M A N U A L</div>
    <h1 class="serif" style="display:inline-flex;align-items:center;gap:10px">
      <span data-i18n="docs_title">Guia SafraSense</span>
      <button class="copy-btn" id="copy-docs-btn" onclick="window.copyDocs(this)" title="Copiar manual completo" data-i18n-title="copy_docs_title" style="margin:0;width:30px;height:30px">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="width:22px;height:22px">
          <rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect>
          <path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path>
        </svg>
      </button>
    </h1>
    <p data-i18n="docs_subtitle">Referência rápida para configuração, monitoramento e cultivo hidropônico.</p>
  </div>
</div>
)rawliteral";

static const char DOCS_FOOTER_HTML[] PROGMEM = R"rawliteral(
</main>
</div>
<script>
(function(){
  var doc=document.documentElement,btn=document.getElementById('themeBtn');
  var moon="<svg viewBox='0 0 24 24' width='20' height='20' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><path d='M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z'/></svg>";
  var sun="<svg viewBox='0 0 24 24' width='20' height='20' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><circle cx='12' cy='12' r='4'/><line x1='12' y1='2' x2='12' y2='6'/><line x1='12' y1='18' x2='12' y2='22'/><line x1='4.93' y1='4.93' x2='7.76' y2='7.76'/><line x1='16.24' y1='16.24' x2='19.07' y2='19.07'/><line x1='2' y1='12' x2='6' y2='12'/><line x1='18' y1='12' x2='22' y2='12'/><line x1='4.93' y1='19.07' x2='7.76' y2='16.24'/><line x1='16.24' y1='7.76' x2='19.07' y2='4.93'/></svg>";
  function setIcon(t){if(btn)btn.innerHTML=t==='dark'?sun:moon;}
  var stored=localStorage.getItem('theme')||(window.matchMedia&&window.matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light');
  doc.setAttribute('data-theme',stored);
  setIcon(stored);
  if(btn)btn.onclick=function(){
    var n=doc.getAttribute('data-theme')==='dark'?'light':'dark';
    doc.setAttribute('data-theme',n);localStorage.setItem('theme',n);setIcon(n);
  };
})();
window.copyDocs = function(btn) {
  const content = document.querySelector('.main.doc-wrap').innerText;
  if (navigator.clipboard && window.isSecureContext) {
    navigator.clipboard.writeText(content).catch(()=>{});
  } else {
    let ta = document.createElement("textarea");
    ta.value = content;
    ta.style.position = "fixed";
    ta.style.opacity = "0";
    document.body.appendChild(ta);
    ta.focus();
    ta.select();
    try { document.execCommand('copy'); } catch(e) {}
    document.body.removeChild(ta);
  }
  if (btn) {
    btn.classList.add('copied');
    const orig = btn.innerHTML;
    btn.innerHTML = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="width:22px;height:22px"><polyline points="20 6 9 17 4 12"></polyline></svg>';
    setTimeout(()=>{btn.classList.remove('copied');btn.innerHTML=orig;}, 1500);
  }
};
function openDocHash(hash,updateHistory){
  if(!hash||hash.charAt(0)!=='#')return;
  var t=document.querySelector(hash);
  if(!t)return;
  var sec=t.tagName==='DETAILS'?t:t.closest('details.doc-section');
  if(sec)sec.open=true;
  setTimeout(function(){t.scrollIntoView({block:'start'});},0);
  if(updateHistory!==false){
    if(history&&history.pushState)history.pushState(null,'',hash);
    else location.hash=hash;
  }
}
function buildSubtoc(item){
  var box=item.querySelector('.doc-subtoc');
  if(!box||box.getAttribute('data-built')==='1')return;
  var sec=document.getElementById(item.getAttribute('data-section'));
  if(!sec)return;
  var heads=sec.querySelectorAll('.doc-body .doc-h4');
  heads.forEach(function(h,i){
    if(!h.id)h.id=sec.id+'-sub-'+(i+1);
    var a=document.createElement('a');
    a.href='#'+h.id;
    a.textContent=h.textContent;
    box.appendChild(a);
  });
  box.setAttribute('data-built','1');
  if(!heads.length){
    var btn=item.querySelector('.doc-toc-toggle');
    if(btn)btn.hidden=true;
  }
}
document.querySelectorAll('.doc-toc-item').forEach(function(item){
  buildSubtoc(item);
  var btn=item.querySelector('.doc-toc-toggle');
  var box=item.querySelector('.doc-subtoc');
  if(btn&&box)btn.addEventListener('click',function(){
    var open=btn.getAttribute('aria-expanded')==='true';
    btn.setAttribute('aria-expanded',open?'false':'true');
    item.classList.toggle('is-open',!open);
    box.hidden=open;
  });
});
document.querySelectorAll('.doc-toc a[href^="#"]').forEach(function(a){
  a.addEventListener('click',function(e){
    e.preventDefault();
    openDocHash(a.getAttribute('href'),true);
  });
});
if(location.hash)openDocHash(location.hash,false);
</script>
<script src="/local-nav.js"></script>
</body>
</html>)rawliteral";

static void handleRaiznet() {
  Language lang = currentLocalLanguage();
  if (gCfg->servers_external.empty()) {
    server.sendHeader("Location", String("/?lang=") + currentLocalLangCode());
    server.send(302, "text/plain", "");
    return;
  }

  String html = R"HTML(<!DOCTYPE html>
<html lang="pt-BR"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Raiznet - SafraSense</title>
<script>(function(){try{var u=new URL(location.href);if(!u.searchParams.has('lang')){var l=localStorage.getItem('lang');if(l){l=String(l).toLowerCase();l=(l==='1'||l==='pt')?'1':((l==='2'||l==='es')?'2':((l==='3'||l==='ja')?'3':((l==='4'||l==='zh'||l.indexOf('zh-')===0)?'4':'0')));u.searchParams.set('lang',l);location.replace(u.pathname+u.search+u.hash);return;}}}catch(_){}var t='light';try{t=localStorage.getItem('theme')||(window.matchMedia&&window.matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light');}catch(_){}document.documentElement.setAttribute('data-theme',t);})();</script>
<link rel="stylesheet" href="/local.css">
</head><body>
<header class="local-header">
  <a class="local-brand" href="/">
    <span class="local-brand-title">S A F R A S E N S E <span class="brand-aqua">A Q U A</span></span>
  </a>
  <nav class="local-tabs" aria-label="Navegação principal" data-i18n-aria-label="nav_main">
    <a class="local-tab" href="/" data-i18n="nav_home">Início</a>
    <a class="local-tab is-active" id="raiznet-menu-item" href="/raiznet" data-i18n="nav_raiznet">Raiznet</a>
    <a class="local-tab" href="/config" data-i18n="nav_config">Configurações</a>
    <a class="local-tab" href="/docs" data-i18n="nav_docs">Manual</a>
  </nav>
  <div class="header-actions">
    <select class="lang-select" id="langSelect" aria-label="Selecionar idioma" data-i18n-aria-label="select_language">
      <option value="1">PT</option><option value="0">EN</option><option value="2">ES</option><option value="3">JA</option><option value="4">ZH</option>
    </select>
    <button class="theme-btn local-theme" id="themeBtn" type="button" aria-label="Alternar tema" data-i18n-aria-label="toggle_theme"></button>
  </div>
</header>
<div class="portal-shell">
  <main class="main">
    <div class="topbar">
      <div class="title">
        <div class="eyebrow" data-i18n="raiznet_label">R E D E   D E S C E N T R A L I Z A D A</div>
        <h1 class="serif" data-i18n="raiznet_heading">Status Raiznet</h1>
      </div>
    </div>
    <div class="content-grid" style="grid-template-columns: 1fr;">
      <div class="panel">
        <div class="section-head"><div class="eyebrow" data-i18n="connected_servers">Servidores Conectados</div></div>
        <div id="externalServers" class="server-list">
          <div class="empty" data-i18n="loading_status">Carregando status...</div>
        </div>
      </div>
    </div>
  </main>
</div>
<script src="/local-nav.js"></script>
<script src="/dashboard.js"></script>
</body></html>)HTML";

  localizeLocalHtml(html, lang, "title_raiznet");
  server.send(200, "text/html", html);
}

static void handleDocs() {
  Language lang = currentLocalLanguage();
  String html;
  html.reserve(22000);
  html += FPSTR(DOCS_HEADER_HTML);
  if (!gCfg->servers_external.empty()) {
    html.replace("id=\"raiznet-menu-item\" href=\"/raiznet\" style=\"display:none\"", "id=\"raiznet-menu-item\" href=\"/raiznet\"");
  }
  html.replace("class=\"local-tab\" href=\"/docs\"", "class=\"local-tab is-active\" href=\"/docs\"");
  appendDocsContent(html, lang);
  html += FPSTR(DOCS_FOOTER_HTML);
  localizeLocalHtml(html, lang, "title_manual");
  server.send(200, "text/html", html);
}

// ── 404 route ─────────────────────────────────────────────────────────────

static void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ── Public interface ──────────────────────────────────────────────────────

void initHttpServer(DeviceConfig* cfg, const DeviceIdentity* id) {
  gCfg = cfg;
  gId  = id;
  server.collectHeaders(LOCAL_HEADER_KEYS, 1);

  server.on("/",                      handleRoot);
  server.on("/raiznet",               handleRaiznet);
  server.on("/local.css",             handleLocalCss);
  server.on("/dashboard.js",          handleDashboardJs);
  server.on("/local-nav.js",          handleLocalNavJs);
  server.on("/api/status",            handleApiStatus);
  server.on("/api/telemetry",         handleApiTelemetry);
  server.on("/api/force-read", []() {
    gPendingAction = ACTION_FORCE_READ;
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/ph/manual", HTTP_POST, []() {
    if (server.hasArg("ph")) {
      float ph = server.arg("ph").toFloat();
      if (ph >= 0 && ph <= 14) {
        gLastReading.ph = ph;
        gHasReading = true;
        // Also add to buffer so it gets sent
        bufferAdd(gLastReading);
        server.send(200, "application/json", "{\"ok\":true}");
        return;
      }
    }
    server.send(400, "application/json", "{\"error\":\"invalid ph\"}");
  });
  server.on("/config",                handleConfig);
  server.on("/config/save",HTTP_POST, handleConfigSave);
  server.on("/docs",                  handleDocs);
  server.on("/reset/wifi",            handleResetWifi);
  server.on("/reset/factory",         handleResetFactoryPage);
  server.on("/reset/factory/confirm", HTTP_POST, handleResetFactoryConfirm);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("[http] Servidor local iniciado na porta 80.");
}

void handleHttpClients() {
  server.handleClient();
}

void updateLastReading(const SensorData& d) {
  gLastReading = d;
  gHasReading  = true;
}

PendingAction getPendingAction() {
  PendingAction a = gPendingAction;
  gPendingAction  = ACTION_NONE;
  return a;
}
