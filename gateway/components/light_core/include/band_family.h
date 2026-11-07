#pragma once
// Bandfamilien: Erkennung (Name, Herstellerdaten, GATT) ohne BLE-Aufrufe, damit sie auf dem Host testbar ist.
// Ablauf im Gateway (gateway/main/ble_link.cpp):
//  1. Scan: Kandidaten sind Geraete, deren Name zu einer Familie passt (fromName). Der Name ist nur ein Hinweis,
//     "LEDBLE" z. B. kommt bei ELK und Triones vor.
//  2. Nach dem Verbinden entscheidet die GATT-Struktur (matchGatt): gesucht wird das erste bekannte Paar aus Service
//     und Characteristic, Profile der Namensfamilie zuerst.
//  3. Zengge braucht zusaetzlich Produkt-ID und BLE-Version aus den Herstellerdaten der Werbung (parseZenggeMfg)
//     oder ersatzweise aus dem langen Namen (parseZenggeName), um das richtige Farbpaket zu waehlen.
// Befehle und Quellen je Familie: hardware/led-band/.
#include <stddef.h>
#include <stdint.h>

enum class BandFamily : uint8_t {
  Unknown = 0,
  Elk,     // ELK-BLEDOM / Lotus Lantern (bisher einzige Familie, am Geraet getestet)
  Triones, // Triones / HappyLighting / LEDBlue / Dream~ / QHM, ungetestet
  Zengge,  // Zengge LEDnetWF / IOTWF, ungetestet
  BjLed,   // BJ_LED (App MohuanLED), ungetestet
};

namespace BandFam {
// Kurzname fuer Logs und Konsole ("ELK", "Triones", ...)
const char *name(BandFamily f);

// Familie anhand des Geraetenamens (Praefix). Unknown: kein Kandidat.
BandFamily fromName(const char *name);

// GATT-Eigenschaften wie Bluetooth Core (Characteristic Properties)
constexpr uint8_t PROP_READ = 0x02;
constexpr uint8_t PROP_WRITE_NO_RSP = 0x04;
constexpr uint8_t PROP_WRITE = 0x08;
constexpr uint8_t PROP_NOTIFY = 0x10;

// Gefundene Characteristic (nur 16-Bit-UUIDs; 128-Bit-UUIDs traegt die Plattform als 0 ein)
struct GattChr {
  uint16_t svc = 0;
  uint16_t chr = 0;
  uint16_t handle = 0;
  uint8_t props = 0;
};

// Schreibziel einer Familie. svc 0 = in jedem Service (BJ_LED: Service in den Quellen nicht genannt).
struct Profile {
  BandFamily family;
  uint16_t svc;
  uint16_t chr;
  bool needWriteNoRsp; // ELK: wie Legacy nur mit "Write without Response"
};

// Alle bekannten Profile in Pruefreihenfolge
const Profile *profiles(int *count);

struct Match {
  BandFamily family = BandFamily::Unknown;
  const Profile *profile = nullptr;
  int index = -1; // Index in der GattChr-Liste
};

// Erstes passendes Profil (Profile der Hinweisfamilie zuerst, dann die uebrigen in Tabellenreihenfolge).
// Eine Characteristic passt nur, wenn sie beschreibbar ist (Write oder Write without Response).
bool matchGatt(BandFamily hint, const GattChr *list, int n, Match &out);

// Zengge: Geraeteangaben aus der Werbung
struct ZenggeInfo {
  bool valid = false;
  uint8_t bleVersion = 0;
  uint16_t productId = 0;
};

// Herstellerdaten wie im AD-Feld 0xFF (die ersten zwei Bytes sind die Company-ID, Little Endian).
// Zengge nutzt Company-IDs 0x5A00..0x5AFF; danach: sta, BLE-Version, MAC (6), Produkt-ID (Big Endian).
bool parseZenggeMfg(const uint8_t *data, size_t len, ZenggeInfo &out);

// Ersatz ohne Herstellerdaten: langer Name "LEDnetWF" + 2 Zeichen Version + 4 Hex Produkt-ID + 6 Hex MAC (20 Zeichen).
bool parseZenggeName(const char *name, ZenggeInfo &out);
} // namespace BandFam
