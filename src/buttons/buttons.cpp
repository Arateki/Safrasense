#include "buttons.h"
#include "config.h"

static unsigned long pressStart = 0;
static bool          wasPressed = false;

void initButtons() {
  // INPUT_PULLUP = the pin reads HIGH when released and LOW when pressed.
  // The ESP32 internal resistor provides this pull-up without an external component.
  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
}

ButtonEvent tickButtons() {
  // LOW = pressed (active pull-up).
  bool pressed = (digitalRead(PIN_BOOT_BUTTON) == LOW);
  unsigned long now = millis();

  if (pressed && !wasPressed) {
    pressStart = now;
    wasPressed = true;
  }

  if (!pressed && wasPressed) {
    wasPressed = false;
    unsigned long held = now - pressStart;
    if (held >= BTN_LONG_MS)  return BTN_LONG_PRESS;
    if (held >= BTN_SHORT_MS) return BTN_SHORT_PRESS;
  }

  return BTN_NONE;
}
