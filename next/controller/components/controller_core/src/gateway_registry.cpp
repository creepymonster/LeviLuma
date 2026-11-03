#include "gateway_registry.h"
#include <string.h>
#include "leviluma_link.h"

static constexpr uint8_t FORMAT = 1;
static constexpr size_t ENTRY_MAX = LVP_ID_LEN + 1 + LEVI_KEY_LEN + 1 + 1 + LVP_NAME_MAX;

size_t GatewayRegistry::serialize(const GatewayEntry *e, int n, uint8_t *buf, size_t cap) {
  size_t p = 0;

  if (cap < 2) {
    return 0;
  }
  buf[p++] = FORMAT;
  size_t countPos = p++;
  uint8_t count = 0;

  for (int i = 0; i < n; i++) {
    if (!e[i].used || !e[i].paired) {
      continue;
    }
    size_t al = strnlen(e[i].alias, LVP_NAME_MAX);

    if (p + ENTRY_MAX > cap) {
      return 0;
    }
    memcpy(buf + p, e[i].id, LVP_ID_LEN);
    p += LVP_ID_LEN;
    buf[p++] = e[i].hasKey ? 1 : 0;
    if (e[i].hasKey) {
      memcpy(buf + p, e[i].key, LEVI_KEY_LEN);
      p += LEVI_KEY_LEN;
    }
    buf[p++] = e[i].zbEndpoint;
    buf[p++] = (uint8_t)al;
    memcpy(buf + p, e[i].alias, al);
    p += al;
    count++;
  }
  buf[countPos] = count;
  return p;
}

int GatewayRegistry::deserialize(const uint8_t *buf, size_t len, GatewayEntry *out, int max) {
  if (len < 2 || buf[0] != FORMAT || buf[1] > max) {
    return -1;
  }
  int n = buf[1];
  size_t p = 2;

  for (int i = 0; i < n; i++) {
    GatewayEntry g;
    g.used = true;
    g.paired = true;

    if (p + LVP_ID_LEN + 1 > len) {
      return -1;
    }
    memcpy(g.id, buf + p, LVP_ID_LEN);
    p += LVP_ID_LEN;
    uint8_t flags = buf[p++];

    if (flags > 1) {
      return -1;
    }
    g.hasKey = flags == 1;
    if (g.hasKey) {
      if (p + LEVI_KEY_LEN > len) {
        return -1;
      }
      memcpy(g.key, buf + p, LEVI_KEY_LEN);
      p += LEVI_KEY_LEN;
    }
    if (p + 2 > len) {
      return -1;
    }
    g.zbEndpoint = buf[p++];
    uint8_t al = buf[p++];

    if (al > LVP_NAME_MAX || p + al > len) {
      return -1;
    }
    memcpy(g.alias, buf + p, al);
    g.alias[al] = 0;
    p += al;
    out[i] = g;
  }
  return p == len ? n : -1;
}

void GatewayRegistry::load() {
  for (auto &g : e_) {
    g = GatewayEntry();
  }
  uint8_t buf[2 + MAX_PAIRED * ENTRY_MAX];
  size_t len = sizeof(buf);

  if (!kv_.getBlob(KEY, buf, &len)) {
    return;
  }
  int n = deserialize(buf, len, e_, MAX_PAIRED);

  if (n < 0) {
    for (auto &g : e_) {
      g = GatewayEntry();
    }
  }
}

bool GatewayRegistry::persist() {
  uint8_t buf[2 + MAX_PAIRED * ENTRY_MAX];
  size_t len = serialize(e_, MAX_ENTRIES, buf, sizeof(buf));
  return len > 0 && kv_.setBlob(KEY, buf, len);
}

int GatewayRegistry::find(const uint8_t id[LVP_ID_LEN]) const {
  for (int i = 0; i < MAX_ENTRIES; i++) {
    if (e_[i].used && lvp_id_equal(e_[i].id, id)) {
      return i;
    }
  }
  return -1;
}

int GatewayRegistry::addDiscovered(const uint8_t id[LVP_ID_LEN], uint32_t nowMs) {
  int idx = find(id);

  if (idx >= 0) {
    return idx;
  }
  int victim = -1;

  for (int i = 0; i < MAX_ENTRIES; i++) {
    if (!e_[i].used) {
      victim = i;
      break;
    }
    // unbekannte, nicht in Kopplung befindliche Eintraege duerfen verdraengt werden
    if (!e_[i].paired && e_[i].pairPhase != PairPhase::Pending &&
        (victim < 0 || (uint32_t)(nowMs - e_[i].lastSeenMs) > (uint32_t)(nowMs - e_[victim].lastSeenMs))) {
      victim = i;
    }
  }
  if (victim < 0) {
    return -1;
  }
  e_[victim] = GatewayEntry();
  e_[victim].used = true;
  memcpy(e_[victim].id, id, LVP_ID_LEN);
  e_[victim].lastSeenMs = nowMs;
  return victim;
}

int GatewayRegistry::pairedCount() const {
  int n = 0;
  for (const auto &g : e_) {
    n += (g.used && g.paired) ? 1 : 0;
  }
  return n;
}

lvp_err_t GatewayRegistry::markPaired(int idx) {
  if (!e_[idx].paired && pairedCount() >= MAX_PAIRED) {
    return LVP_ERR_FULL;
  }
  bool was = e_[idx].paired;
  e_[idx].paired = true;

  if (!persist()) {
    e_[idx].paired = was;
    return LVP_ERR_STORAGE;
  }
  return LVP_OK;
}

bool GatewayRegistry::unpair(int idx) {
  GatewayEntry &g = e_[idx];
  g.paired = false;
  g.hasKey = false;
  memset(g.key, 0, sizeof(g.key));
  g.zbEndpoint = 0;
  g.alias[0] = 0;
  g.stateValid = false;
  g.calibValid = false;
  g.pairPhase = PairPhase::None;
  return persist();
}

bool GatewayRegistry::setAlias(int idx, const char *alias) {
  strncpy(e_[idx].alias, alias ? alias : "", LVP_NAME_MAX);
  e_[idx].alias[LVP_NAME_MAX] = 0;
  return e_[idx].paired ? persist() : true;
}

bool GatewayRegistry::online(int i, uint32_t nowMs, uint32_t timeoutMs) const {
  return e_[i].used && lvl_online(e_[i].lastSeenMs, nowMs, timeoutMs);
}
