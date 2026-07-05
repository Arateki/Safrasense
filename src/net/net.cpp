#include "net.h"
#include <WiFi.h>

#ifdef QEMU_EMULATOR
#include <esp_eth.h>
#include <esp_event.h>
#include <esp_netif.h>

static volatile bool     gGotIp = false;
static esp_netif_t*      gNetif = nullptr;

static void onGotIp(void*, esp_event_base_t, int32_t, void*) { gGotIp = true; }

void emuNetStart() {
  // O core Arduino pode já ter criado netif/event loop; erros de
  // "já inicializado" são esperados e inofensivos aqui.
  esp_netif_init();
  esp_event_loop_create_default();

  esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
  gNetif = esp_netif_new(&netif_cfg);

  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
  phy_config.autonego_timeout_ms = 100;
  esp_eth_mac_t* mac = esp_eth_mac_new_openeth(&mac_config);
  esp_eth_phy_t* phy = esp_eth_phy_new_dp83848(&phy_config);

  esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
  esp_eth_handle_t eth_handle = nullptr;
  ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));
  ESP_ERROR_CHECK(esp_netif_attach(gNetif, esp_eth_new_netif_glue(eth_handle)));
  esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &onGotIp, nullptr);
  ESP_ERROR_CHECK(esp_eth_start(eth_handle));

  Serial.print("[net] Esperando IP da Ethernet virtual (QEMU)");
  while (!gGotIp) { delay(100); Serial.print('.'); }
  Serial.println();
  Serial.printf("[net] Conectado, IP %s\n", netLocalIp().c_str());
}

bool netReady() { return gGotIp; }

String netLocalIp() {
  esp_netif_ip_info_t info;
  if (gNetif && esp_netif_get_ip_info(gNetif, &info) == ESP_OK) {
    return IPAddress(info.ip.addr).toString();
  }
  return "0.0.0.0";
}
#else
bool   netReady()   { return WiFi.status() == WL_CONNECTED; }
String netLocalIp() { return WiFi.localIP().toString(); }
#endif
