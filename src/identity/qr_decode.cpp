#include "qr_decode.h"
#include "logging/logging.h"

#include <quirc.h>
#include <mbedtls/base64.h>
#include <string.h>

static bool decodePreparedQuirc(struct quirc* qr, String& payload, String& error) {
  bool ok = false;

  const int count = quirc_count(qr);
  LOG_DEBUGF("qr", "quirc candidates=%d\n", count);
  if (count <= 0) {
    error = "qr_not_found";
  }

  struct quirc_code* code = (struct quirc_code*)malloc(sizeof(struct quirc_code));
  struct quirc_data* data = (struct quirc_data*)malloc(sizeof(struct quirc_data));
  if (!code || !data) {
    error = "decoder_work_alloc_failed";
    LOG_ERRORF("qr", "work alloc failed code=%u data=%u heap=%u\n", code ? 1 : 0, data ? 1 : 0, (unsigned)ESP.getFreeHeap());
  }

  for (int i = 0; code && data && i < count && !ok; i++) {
    quirc_extract(qr, i, code);
    quirc_decode_error_t err = quirc_decode(code, data);
    if (err == QUIRC_ERROR_DATA_ECC) {
      quirc_flip(code);
      err = quirc_decode(code, data);
    }
    if (err == QUIRC_SUCCESS) {
      payload.reserve(data->payload_len + 1);
      for (int j = 0; j < data->payload_len; j++) {
        payload += (char)data->payload[j];
      }
      ok = payload.length() > 0;
      LOG_DEBUGF("qr", "candidate=%d success payload_len=%u\n", i, (unsigned)payload.length());
      if (!ok) error = "empty_payload";
    } else {
      error = quirc_strerror(err);
      LOG_DEBUGF("qr", "candidate=%d error=%s\n", i, error.c_str());
    }
  }

  if (code) free(code);
  if (data) free(data);

  return ok;
}

static bool validateQrSize(int width, int height, size_t length, String& error) {
  if (width <= 0 || height <= 0) {
    error = "invalid_image";
    return false;
  }
  if (width > QR_DECODE_MAX_SIDE || height > QR_DECODE_MAX_SIDE) {
    error = "image_too_large";
    return false;
  }
  const size_t expected = (size_t)width * (size_t)height;
  if (expected == 0 || expected != length) {
    error = "invalid_size";
    return false;
  }
  return true;
}

bool decodeQrGrayscale(const uint8_t* pixels, size_t length, int width, int height, String& payload, String& error) {
  payload = "";
  error = "";

  LOG_DEBUGF("qr", "decode start width=%d height=%d bytes=%u\n", width, height, (unsigned)length);

  if (!pixels || !validateQrSize(width, height, length, error)) {
    LOG_DEBUGF("qr", "decode fail error=%s\n", error.c_str());
    return false;
  }

  struct quirc* qr = quirc_new();
  if (!qr) {
    error = "decoder_alloc_failed";
    LOG_ERRORF("qr", "decode fail error=%s\n", error.c_str());
    return false;
  }

  bool ok = false;
  if (quirc_resize(qr, width, height) < 0) {
    error = "image_alloc_failed";
  } else {
    int qrWidth = 0;
    int qrHeight = 0;
    uint8_t* image = quirc_begin(qr, &qrWidth, &qrHeight);
    if (!image || qrWidth != width || qrHeight != height) {
      error = "decoder_buffer_failed";
    } else {
      memcpy(image, pixels, length);
      quirc_end(qr);
      ok = decodePreparedQuirc(qr, payload, error);
    }
  }

  quirc_destroy(qr);
  if (!ok) {
    LOG_DEBUGF("qr", "decode fail error=%s\n", error.length() ? error.c_str() : "unknown");
  }
  return ok;
}

bool decodeQrBase64PackedBitmap(const String& encodedPixels, int width, int height, String& payload, String& error) {
  payload = "";
  error = "";

  const size_t imageBytes = (size_t)width * (size_t)height;
  const size_t packedBytes = (imageBytes + 7) / 8;
  LOG_DEBUGF("qr", "decode packed b64 start width=%d height=%d image_bytes=%u packed_expected=%u heap=%u\n", width, height, (unsigned)imageBytes, (unsigned)packedBytes, (unsigned)ESP.getFreeHeap());
  if (!validateQrSize(width, height, imageBytes, error)) {
    LOG_DEBUGF("qr", "decode fail error=%s\n", error.c_str());
    return false;
  }

  struct quirc* qr = quirc_new();
  if (!qr) {
    error = "decoder_alloc_failed";
    LOG_ERRORF("qr", "decode fail error=%s heap=%u\n", error.c_str(), (unsigned)ESP.getFreeHeap());
    return false;
  }

  bool ok = false;
  if (quirc_resize(qr, width, height) < 0) {
    error = "image_alloc_failed";
  } else {
    int qrWidth = 0;
    int qrHeight = 0;
    uint8_t* image = quirc_begin(qr, &qrWidth, &qrHeight);
    if (!image || qrWidth != width || qrHeight != height) {
      error = "decoder_buffer_failed";
    } else {
      uint8_t* packed = (uint8_t*)malloc(packedBytes);
      if (!packed) {
        error = "packed_alloc_failed";
        LOG_ERRORF("qr", "packed alloc failed bytes=%u heap=%u\n", (unsigned)packedBytes, (unsigned)ESP.getFreeHeap());
      } else {
        size_t decodedLength = 0;
        const int b64Result = mbedtls_base64_decode(packed, packedBytes, &decodedLength, (const unsigned char*)encodedPixels.c_str(), encodedPixels.length());
        const bool decoded = b64Result == 0 && decodedLength == packedBytes;
        LOG_DEBUGF("qr", "packed b64 result=%d decoded=%u expected=%u ok=%d heap=%u\n", b64Result, (unsigned)decodedLength, (unsigned)packedBytes, decoded, (unsigned)ESP.getFreeHeap());
        if (decoded) {
          for (size_t i = 0; i < imageBytes; i++) {
            const bool dark = (packed[i >> 3] & (1 << (7 - (i & 7)))) != 0;
            image[i] = dark ? 0 : 255;
          }
          quirc_end(qr);
          ok = decodePreparedQuirc(qr, payload, error);
        } else {
          error = "invalid_qr_payload";
        }
        free(packed);
      }
    }
  }

  quirc_destroy(qr);
  if (!ok) {
    LOG_DEBUGF("qr", "decode fail error=%s\n", error.length() ? error.c_str() : "unknown");
  }
  return ok;
}
