#include "levi_key.h"

static int hexval(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

bool levi_key_is_zero(const uint8_t key[LEVI_KEY_LEN]) {
  uint8_t acc = 0;
  for (int i = 0; i < LEVI_KEY_LEN; i++) {
    acc |= key[i];
  }
  return acc == 0;
}

bool levi_key_parse_hex(const char *s, uint8_t out[LEVI_KEY_LEN]) {
  int n = 0;
  uint8_t tmp[LEVI_KEY_LEN] = {0};

  if (!s) {
    return false;
  }
  for (; *s; s++) {
    if (*s == ' ' || *s == ':' || *s == '-') {
      continue;
    }
    int v = hexval(*s);
    if (v < 0 || n >= LEVI_KEY_HEX_LEN) {
      return false;
    }
    tmp[n / 2] = (uint8_t)(tmp[n / 2] | (n % 2 ? v : v << 4));
    n++;
  }
  if (n != LEVI_KEY_HEX_LEN || levi_key_is_zero(tmp)) {
    return false;
  }
  for (int i = 0; i < LEVI_KEY_LEN; i++) {
    out[i] = tmp[i];
  }
  return true;
}

void levi_key_to_hex(const uint8_t key[LEVI_KEY_LEN], char *out, size_t cap) {
  static const char H[] = "0123456789ABCDEF";
  size_t p = 0;

  for (int i = 0; i < LEVI_KEY_LEN && p + 2 < cap; i++) {
    out[p++] = H[key[i] >> 4];
    out[p++] = H[key[i] & 0x0F];
  }
  if (cap) {
    out[p < cap ? p : cap - 1] = 0;
  }
}
