#pragma once
#include <Arduino.h>

enum ButtonEvent {
  BTN_NONE,
  BTN_SHORT_PRESS,  // 2s -> reconnect Wi-Fi
  BTN_LONG_PRESS,   // 5s -> full reset
};

void        initButtons();
ButtonEvent tickButtons();
