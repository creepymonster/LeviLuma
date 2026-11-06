#include "pairing_table.h"
#include <string.h>

static constexpr uint8_t FORMAT = 1;

size_t PairingTable::serialize(const Entry *e, int n, uint8_t *buf, size_t cap) {
  size_t p = 0;

  if (cap < 2) {
    return 0;
  }
  buf[p++] = FORMAT;
  buf[p++] = (uint8_t)n;

  for (int i = 0; i < n; i++) {
    size_t nl = strlen(e[i].name);
    if (nl > LVP_NAME_MAX) {
      nl = LVP_NAME_MAX;
    }
    if (p + LVP_ID_LEN + 1 + nl > cap) {
      return 0;
    }
    memcpy(buf + p, e[i].mac, LVP_ID_LEN);
    p += LVP_ID_LEN;
    buf[p++] = (uint8_t)nl;
    memcpy(buf + p, e[i].name, nl);
    p += nl;
  }
  return p;
}

int PairingTable::deserialize(const uint8_t *buf, size_t len, Entry *out, int max) {
  if (len < 2 || buf[0] != FORMAT || buf[1] > max) {
    return -1;
  }
  int n = buf[1];
  size_t p = 2;

  for (int i = 0; i < n; i++) {
    if (p + LVP_ID_LEN + 1 > len) {
      return -1;
    }
    memcpy(out[i].mac, buf + p, LVP_ID_LEN);
    p += LVP_ID_LEN;
    uint8_t nl = buf[p++];

    if (nl > LVP_NAME_MAX || p + nl > len) {
      return -1;
    }
    memcpy(out[i].name, buf + p, nl);
    out[i].name[nl] = 0;
    p += nl;
  }
  return p == len ? n : -1;
}

void PairingTable::load() {
  uint8_t buf[2 + MAX * (LVP_ID_LEN + 1 + LVP_NAME_MAX)];
  size_t len = sizeof(buf);
  n_ = 0;

  if (kv_.getBlob(KEY, buf, &len)) {
    int n = deserialize(buf, len, e_, MAX);
    n_ = n < 0 ? 0 : n;
  }
}

bool PairingTable::persist() {
  uint8_t buf[2 + MAX * (LVP_ID_LEN + 1 + LVP_NAME_MAX)];
  size_t len = serialize(e_, n_, buf, sizeof(buf));
  return len > 0 && kv_.setBlob(KEY, buf, len);
}

bool PairingTable::contains(const uint8_t mac[LVP_ID_LEN]) const {
  for (int i = 0; i < n_; i++) {
    if (lvp_id_equal(e_[i].mac, mac)) {
      return true;
    }
  }
  return false;
}

lvp_err_t PairingTable::add(const uint8_t mac[LVP_ID_LEN], const char *name) {
  Entry *slot = nullptr;
  Entry backup[MAX];
  int backupN = n_;
  memcpy(backup, e_, sizeof(e_));

  for (int i = 0; i < n_; i++) {
    if (lvp_id_equal(e_[i].mac, mac)) {
      slot = &e_[i];
    }
  }
  if (!slot) {
    if (n_ >= MAX) {
      return LVP_ERR_FULL;
    }
    slot = &e_[n_++];
    memcpy(slot->mac, mac, LVP_ID_LEN);
  }
  strncpy(slot->name, name ? name : "", LVP_NAME_MAX);
  slot->name[LVP_NAME_MAX] = 0;

  if (!persist()) {
    memcpy(e_, backup, sizeof(e_));
    n_ = backupN;
    return LVP_ERR_STORAGE;
  }
  return LVP_OK;
}

bool PairingTable::remove(const uint8_t mac[LVP_ID_LEN]) {
  for (int i = 0; i < n_; i++) {
    if (lvp_id_equal(e_[i].mac, mac)) {
      e_[i] = e_[n_ - 1];
      n_--;
      return persist();
    }
  }
  return true; // war nicht gekoppelt
}

bool PairingTable::clear() {
  n_ = 0;
  return kv_.erase(KEY);
}
