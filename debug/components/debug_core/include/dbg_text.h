#pragma once
// Begrenzter Textpuffer fuer die Ausgabe (kein Heap, schneidet bei Ueberlauf ab und merkt es sich).
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

namespace dbg {

class Out {
public:
  Out(char *buf, size_t cap) : buf_(buf), cap_(cap) { clear(); }
  void clear() {
    len_ = 0;
    truncated_ = false;
    if (cap_) {
      buf_[0] = 0;
    }
  }
  void printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
  void vprintf(const char *fmt, va_list ap);
  void puts(const char *s);
  const char *c_str() const { return buf_; }
  size_t size() const { return len_; }
  bool truncated() const { return truncated_; }

private:
  char *buf_;
  size_t cap_;
  size_t len_ = 0;
  bool truncated_ = false;
};

// "AA:BB:CC:DD:EE:FF" (out mindestens 18 Byte)
void mac_to_str(const uint8_t mac[6], char *out, size_t cap);
// Liest "AA:BB:CC:DD:EE:FF" (auch mit '-' und Kleinbuchstaben). Genau sechs Bytes, sonst false.
bool mac_parse(const char *s, uint8_t mac[6]);
bool mac_is_broadcast(const uint8_t mac[6]);

// Hexdump mit Offset, 16 Byte je Zeile, optional ASCII-Spalte. indent wird vor jede Zeile gesetzt.
void hexdump(Out &out, const char *indent, const uint8_t *data, size_t len, bool ascii);

} // namespace dbg
