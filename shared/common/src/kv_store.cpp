#include "kv_store.h"
#include <string.h>

bool MemKvStore::getBlob(const char *key, void *buf, size_t *len) {
  auto it = data.find(key);

  if (it == data.end() || it->second.size() > *len) {
    return false;
  }
  memcpy(buf, it->second.data(), it->second.size());
  *len = it->second.size();
  return true;
}

bool MemKvStore::blobSize(const char *key, size_t *len) {
  auto it = data.find(key);

  if (it == data.end()) {
    return false;
  }
  *len = it->second.size();
  return true;
}

bool MemKvStore::setBlob(const char *key, const void *buf, size_t len) {
  if (failWrites) {
    return false;
  }
  const uint8_t *p = (const uint8_t *)buf;
  data[key] = std::vector<uint8_t>(p, p + len);
  return true;
}

bool MemKvStore::erase(const char *key) {
  if (failWrites) {
    return false;
  }
  data.erase(key);
  return true;
}

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "kv";

bool NvsKvStore::getBlob(const char *key, void *buf, size_t *len) {
  nvs_handle_t h;

  if (nvs_open(ns_, NVS_READONLY, &h) != ESP_OK) {
    return false; // Namensraum existiert noch nicht
  }
  esp_err_t e = nvs_get_blob(h, key, buf, len);
  nvs_close(h);

  if (e != ESP_OK && e != ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "NVS: %s/%s lesen fehlgeschlagen (%s)", ns_, key, esp_err_to_name(e));
  }
  return e == ESP_OK;
}

bool NvsKvStore::blobSize(const char *key, size_t *len) {
  nvs_handle_t h;

  if (nvs_open(ns_, NVS_READONLY, &h) != ESP_OK) {
    return false;
  }
  *len = 0;
  esp_err_t e = nvs_get_blob(h, key, nullptr, len);
  nvs_close(h);
  return e == ESP_OK;
}

bool NvsKvStore::setBlob(const char *key, const void *buf, size_t len) {
  nvs_handle_t h;
  esp_err_t e = nvs_open(ns_, NVS_READWRITE, &h);

  if (e != ESP_OK) {
    ESP_LOGE(TAG, "NVS: %s oeffnen fehlgeschlagen (%s)", ns_, esp_err_to_name(e));
    return false;
  }
  e = nvs_set_blob(h, key, buf, len);

  if (e == ESP_OK) {
    e = nvs_commit(h);
  }
  nvs_close(h);

  if (e != ESP_OK) {
    ESP_LOGE(TAG, "NVS: %s/%s schreiben fehlgeschlagen (%s)", ns_, key, esp_err_to_name(e));
  }
  return e == ESP_OK;
}

bool NvsKvStore::erase(const char *key) {
  nvs_handle_t h;
  esp_err_t e = nvs_open(ns_, NVS_READWRITE, &h);

  if (e != ESP_OK) {
    return false;
  }
  e = nvs_erase_key(h, key);

  if (e == ESP_ERR_NVS_NOT_FOUND) {
    e = ESP_OK;
  }
  if (e == ESP_OK) {
    e = nvs_commit(h);
  }
  nvs_close(h);
  return e == ESP_OK;
}
#endif
