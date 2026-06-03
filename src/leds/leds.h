#pragma once
#include <Arduino.h>

enum LedState {
  LED_BOOTING,          // solid yellow
  LED_PORTAL_OPEN,      // alternating yellow + red
  LED_WIFI_CONNECTING,  // slow yellow blink
  LED_NORMAL,           // LEDs off (green blinks when sending)
  LED_SERVER_OFFLINE,   // yellow blinks every 5s
  LED_SENSOR_FAIL,      // red blinks every 5s
  LED_RESET_SHORT,      // two quick yellow blinks
  LED_RESET_LONG,       // all LEDs blink red 3x before reboot
};

void initLeds();
void setLedState(LedState s);
void tickLeds();
void blinkOnSend();  // quick green blink after a successful send
