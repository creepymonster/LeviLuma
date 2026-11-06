#include "dbg_text.h"
#include <stdio.h>
#include <string.h>

namespace dbg {

void Out::vprintf(const char *fmt, va_list ap) {
  if (!cap_ || truncated_) {
    return;
  }
  size_t room = cap_ - len_; // enthaelt das Nullbyte
  int n = vsnprintf(buf_ + len_, room, fmt, ap);

  if (n < 0) {
    buf_[len_] = 0;
    truncated_ = true;
    return;
  }
  if ((size_t)n >= room) {
    len_ = cap_ - 1;
    truncated_ = true;
  } else {
    len_ += (size_t)n;
  }
}

void Out::printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

void Out::puts(const char *s) {
  printf("%s", s);
}

void mac_to_str(const uint8_t mac[6], char *out, size_t cap) {
  snprintf(out, cap, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

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

bool mac_parse(const char *s, uint8_t mac[6]) {
  if (!s) {
    return false;
  }
  uint8_t tmp[6];

  for (int i = 0; i < 6; i++) {
    int hi = hexval(s[0]), lo = hi < 0 ? -1 : hexval(s[1]);

    if (hi < 0 || lo < 0) {
      return false;
    }
    tmp[i] = (uint8_t)(hi << 4 | lo);
    s += 2;

    if (i < 5) {
      if (*s != ':' && *s != '-') {
        return false;
      }
      s++;
    }
  }
  if (*s) {
    return false;
  }
  memcpy(mac, tmp, 6);
  return true;
}

bool mac_is_broadcast(const uint8_t mac[6]) {
  for (int i = 0; i < 6; i++) {
    if (mac[i] != 0xFF) {
      return false;
    }
  }
  return true;
}

void hexdump(Out &out, const char *indent, const uint8_t *data, size_t len, bool ascii) {
  if (len == 0) {
    out.printf("%s(leer)\n", indent);
    return;
  }
  for (size_t off = 0; off < len; off += 16) {
    size_t n = len - off < 16 ? len - off : 16;
    out.printf("%s%04X ", indent, (unsigned)off);

    for (size_t i = 0; i < 16; i++) {
      if (i < n) {
        out.printf(" %02X", data[off + i]);
      } else {
        out.puts("   ");
      }
    }
    if (ascii) {
      out.puts("  |");
      for (size_t i = 0; i < n; i++) {
        uint8_t c = data[off + i];
        out.printf("%c", (c >= 0x20 && c < 0x7F) ? (char)c : '.');
      }
      out.puts("|");
    }
    out.puts("\n");
  }
}

} // namespace dbg
