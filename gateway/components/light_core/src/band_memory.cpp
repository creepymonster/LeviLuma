#include "band_memory.h"
#include "band_family.h"
#include <stdio.h>
#include <string.h>

void BandMemory::load() {
  uint8_t buf[BLOB_LEN];
  size_t len = sizeof(buf);
  has_ = kv_.getBlob(KEY, buf, &len) && deserialize(buf, len, addr_);
  if (!has_) {
    addr_ = Addr();
  }
}

bool BandMemory::remember(const Addr &a) {
  uint8_t buf[BLOB_LEN];
  size_t n = serialize(a, buf, sizeof(buf));
  if (!n || !kv_.setBlob(KEY, buf, n)) {
    return false;
  }
  addr_ = a;
  has_ = true;
  return true;
}

bool BandMemory::forget() {
  if (!kv_.erase(KEY)) {
    return false;
  }
  addr_ = Addr();
  has_ = false;
  return true;
}

BandMemory::Choice BandMemory::choose(const Candidate *c, int n) const {
  Choice ch;
  if (!c || n <= 0) {
    ch.reason = has_ ? Reason::RememberedMissing : Reason::None;
    return ch;
  }
  if (has_) {
    for (int i = 0; i < n; i++) {
      if (sameAddr(c[i].addr, addr_)) {
        ch.index = i; // auch ohne empfangenen Namen
      } else if (nameMatches(c[i].name)) {
        ch.ignored++;
      }
    }
    ch.reason = ch.index >= 0 ? Reason::Remembered : Reason::RememberedMissing;
    return ch;
  }
  for (int i = 0; i < n; i++) {
    if (nameMatches(c[i].name) && (ch.index < 0 || c[i].rssi > c[ch.index].rssi)) {
      ch.index = i;
    }
  }
  ch.reason = ch.index >= 0 ? Reason::Strongest : Reason::None;
  return ch;
}

bool BandMemory::nameMatches(const char *name) {
  return BandFam::fromName(name) != BandFamily::Unknown;
}

bool BandMemory::sameAddr(const Addr &a, const Addr &b) {
  return a.type == b.type && memcmp(a.val, b.val, sizeof(a.val)) == 0;
}

void BandMemory::addrToStr(const Addr &a, char *out, size_t cap) {
  if (!out || !cap) {
    return;
  }
  snprintf(out, cap, "%02x:%02x:%02x:%02x:%02x:%02x", a.val[5], a.val[4], a.val[3], a.val[2], a.val[1], a.val[0]);
}

size_t BandMemory::serialize(const Addr &a, uint8_t *buf, size_t cap) {
  if (!buf || cap < BLOB_LEN) {
    return 0;
  }
  buf[0] = 1; // Version
  buf[1] = a.type;
  memcpy(buf + 2, a.val, sizeof(a.val));
  return BLOB_LEN;
}

bool BandMemory::deserialize(const uint8_t *buf, size_t len, Addr &out) {
  if (!buf || len != BLOB_LEN || buf[0] != 1) {
    return false;
  }
  out.type = buf[1];
  memcpy(out.val, buf + 2, sizeof(out.val));
  return true;
}
