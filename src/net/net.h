#pragma once
#include <Arduino.h>

// Abstrai "a rede está de pé?" para produção (Wi-Fi) e emulador (Ethernet
// virtual do QEMU). Só os call sites de status usam isto; o resto do código
// (HTTPClient/WiFiClient) é TCP genérico e não muda.
bool   netReady();
String netLocalIp();

#ifdef QEMU_EMULATOR
// Sobe o driver openeth e bloqueia até obter IP via DHCP (slirp).
void emuNetStart();
#endif
