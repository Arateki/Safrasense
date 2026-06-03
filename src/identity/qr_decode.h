#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

static const int QR_DECODE_MAX_SIDE = 240;

bool decodeQrGrayscale(const uint8_t* pixels, size_t length, int width, int height, String& payload, String& error);
bool decodeQrBase64PackedBitmap(const String& encodedPixels, int width, int height, String& payload, String& error);
