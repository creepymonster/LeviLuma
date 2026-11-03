#pragma once
// Schluessel-Wert-Speicher als Schnittstelle: NVS auf dem Geraet, Speicher im RAM fuer Host-Tests.
// Alle persistenten Daten (Kalibrierung, Kopplungen, Schluessel, Zuordnungen) laufen hierueber,
// damit die Logik ohne Hardware testbar ist.
#include <stddef.h>
#include <stdint.h>
#include <map>
#include <string>
#include <vector>

class KvStore {
public:
  virtual ~KvStore() = default;
  // Liest einen Blob. *len: Eingabe Puffergroesse, Ausgabe tatsaechliche Laenge.
  // false, wenn der Schluessel fehlt, der Puffer zu klein ist oder ein Fehler auftrat.
  virtual bool getBlob(const char *key, void *buf, size_t *len) = 0;
  // Laenge eines gespeicherten Blobs, false wenn nicht vorhanden.
  virtual bool blobSize(const char *key, size_t *len) = 0;
  // Schreibt und bestaetigt (commit). false bei Fehler.
  virtual bool setBlob(const char *key, const void *buf, size_t len) = 0;
  // Entfernt den Schluessel. true auch dann, wenn er nicht vorhanden war.
  virtual bool erase(const char *key) = 0;
};

// RAM-Variante (Host-Tests). Kann Schreibfehler simulieren.
class MemKvStore : public KvStore {
public:
  bool failWrites = false;
  std::map<std::string, std::vector<uint8_t>> data;

  bool getBlob(const char *key, void *buf, size_t *len) override;
  bool blobSize(const char *key, size_t *len) override;
  bool setBlob(const char *key, const void *buf, size_t len) override;
  bool erase(const char *key) override;
};

#ifdef ESP_PLATFORM
// NVS-Variante (ein Namensraum je Instanz). Schluesselnamen hoechstens 15 Zeichen (NVS-Grenze).
class NvsKvStore : public KvStore {
public:
  explicit NvsKvStore(const char *ns) : ns_(ns) {}
  bool getBlob(const char *key, void *buf, size_t *len) override;
  bool blobSize(const char *key, size_t *len) override;
  bool setBlob(const char *key, const void *buf, size_t len) override;
  bool erase(const char *key) override;

private:
  const char *ns_;
};
#endif
