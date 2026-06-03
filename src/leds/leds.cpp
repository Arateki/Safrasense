#include "leds.h"
#include "config.h"

static LedState      currentState = LED_BOOTING;
static unsigned long lastTick     = 0;
static int           tickPhase    = 0;

void initLeds() {
  pinMode(PIN_LED_RED,    OUTPUT);
  pinMode(PIN_LED_YELLOW, OUTPUT);
  pinMode(PIN_LED_GREEN,  OUTPUT);
  digitalWrite(PIN_LED_RED,    LOW);
  digitalWrite(PIN_LED_YELLOW, LOW);
  digitalWrite(PIN_LED_GREEN,  LOW);
}

static void rgb(bool r, bool y, bool g) {
  digitalWrite(PIN_LED_RED,    r ? HIGH : LOW);
  digitalWrite(PIN_LED_YELLOW, y ? HIGH : LOW);
  digitalWrite(PIN_LED_GREEN,  g ? HIGH : LOW);
}

void setLedState(LedState s) {
  if (s == currentState) return;  // avoid resetting the timer if state did not change
  currentState = s;
  tickPhase    = 0;
  lastTick     = millis();

  // Immediate output for static states.
  switch (s) {
    case LED_BOOTING:         rgb(false, true,  false); break;
    case LED_WIFI_CONNECTING: rgb(false, false, false); break;
    case LED_NORMAL:          rgb(false, false, false); break;
    default: break;
  }
}

// Quick green blink after sending; does not change the current state.
void blinkOnSend() {
  digitalWrite(PIN_LED_GREEN, HIGH);
  delay(80);
  digitalWrite(PIN_LED_GREEN, LOW);
}

void tickLeds() {
  unsigned long now = millis();

  switch (currentState) {

    case LED_PORTAL_OPEN:
      // Yellow and red alternating every 300ms.
      if (now - lastTick >= 300) {
        lastTick = now;
        tickPhase = !tickPhase;
        rgb(tickPhase, !tickPhase, false);
      }
      break;

    case LED_WIFI_CONNECTING:
      // Slow yellow blink (800ms).
      if (now - lastTick >= 800) {
        lastTick = now;
        tickPhase = !tickPhase;
        rgb(false, tickPhase, false);
      }
      break;

    case LED_SERVER_OFFLINE:
      // Yellow: one short blink every 5s.
      if (now - lastTick >= 5000) {
        lastTick = now;
        rgb(false, true, false);
        delay(150);
        rgb(false, false, false);
      }
      break;

    case LED_SENSOR_FAIL:
      // Red: one short blink every 5s.
      if (now - lastTick >= 5000) {
        lastTick = now;
        rgb(true, false, false);
        delay(150);
        rgb(false, false, false);
      }
      break;

    case LED_RESET_SHORT:
      // Yellow: two quick blinks, then stop.
      if (tickPhase < 4 && now - lastTick >= 150) {
        lastTick = now;
        rgb(false, (tickPhase % 2 == 0), false);
        tickPhase++;
      }
      break;

    case LED_RESET_LONG:
      // All LEDs: three quick blinks; reboot happens right after.
      if (tickPhase < 6 && now - lastTick >= 150) {
        lastTick = now;
        bool on = (tickPhase % 2 == 0);
        rgb(on, on, on);
        tickPhase++;
      }
      break;

    default: break;
  }
}
