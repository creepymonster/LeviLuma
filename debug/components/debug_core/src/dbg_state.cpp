#include "dbg_state.h"
#include <string.h>

namespace dbg {

void format_stats(Out &out, const Stats &s, uint32_t uptime_ms) {
  out.printf("Statistik (Laufzeit %lu.%03lu s)\n", (unsigned long)(uptime_ms / 1000), (unsigned long)(uptime_ms % 1000));
  out.printf("  Management-Rahmen gesehen : %lu (davon kein ESP-NOW-Kandidat, ignoriert: %lu)\n", (unsigned long)s.mgmt_seen, (unsigned long)s.ignored);
  out.printf("  ESP-NOW-Callback-Rahmen   : %lu (Vergleichspfad)\n", (unsigned long)s.cb_seen);
  out.printf("  In Queue gestellt         : %lu, verarbeitet: %lu\n", (unsigned long)s.queued, (unsigned long)s.processed);
  out.printf("  VERWORFEN (Queue voll)    : %lu%s\n", (unsigned long)s.queue_drops, s.queue_drops ? "  <-- Nachrichten gingen verloren" : "");
  out.printf("  Beim Kopieren gekuerzt    : %lu (Rahmen groesser als %u Byte)\n", (unsigned long)s.rx_cut, (unsigned)MAX_CAPTURE);
  out.printf("  Doppelt (zweiter Weg)     : %lu unterdrueckt\n", (unsigned long)s.dup_dropped);
  out.printf("  Durch Filter ausgeblendet : %lu, ausgegeben: %lu\n", (unsigned long)s.filtered, (unsigned long)s.printed);
  out.printf("  Verschluesselt            : %lu (Inhalt nicht lesbar)\n", (unsigned long)s.encrypted);
  out.printf("  Kein ESP-NOW/abgeschnitten/beschaedigt: %lu / %lu / %lu\n", (unsigned long)s.not_espnow, (unsigned long)s.truncated, (unsigned long)s.malformed);
  out.printf("  LeviLuma gueltig          : %lu, CRC falsch: %lu, sonstige Fehler: %lu, ESP-NOW ohne LeviLuma: %lu\n", (unsigned long)s.levi_ok,
             (unsigned long)s.levi_crc_bad, (unsigned long)s.levi_other_err, (unsigned long)s.not_levi);
  out.printf("  Ausgabe gekuerzt          : %lu\n", (unsigned long)s.out_truncated);
  out.puts("  Verarbeitet je Kanal      :");
  bool any = false;

  for (int ch = 0; ch < 15; ch++) {
    if (s.by_channel[ch]) {
      if (ch) {
        out.printf(" K%d=%lu", ch, (unsigned long)s.by_channel[ch]);
      } else {
        out.printf(" unbekannt=%lu", (unsigned long)s.by_channel[ch]);
      }
      any = true;
    }
  }
  out.puts(any ? "\n" : " -\n");
}

// ---- Filter ----

bool Filter::type_active() const {
  for (size_t i = 0; i < sizeof(type_bits); i++) {
    if (type_bits[i]) {
      return true;
    }
  }
  return false;
}

bool Filter::add_mac(const uint8_t mac[6]) {
  for (int i = 0; i < nmacs; i++) {
    if (memcmp(macs[i], mac, 6) == 0) {
      return true;
    }
  }
  if (nmacs >= MAX_MACS) {
    return false;
  }
  memcpy(macs[nmacs++], mac, 6);
  return true;
}

bool Filter::del_mac(const uint8_t mac[6]) {
  for (int i = 0; i < nmacs; i++) {
    if (memcmp(macs[i], mac, 6) == 0) {
      memmove(macs[i], macs[i + 1], (size_t)(nmacs - i - 1) * 6);
      nmacs--;
      return true;
    }
  }
  return false;
}

void Filter::set_type(uint8_t t, bool on) {
  if (on) {
    type_bits[t >> 3] |= (uint8_t)(1u << (t & 7));
  } else {
    type_bits[t >> 3] &= (uint8_t)~(1u << (t & 7));
  }
}

void Filter::clear() {
  *this = Filter();
}

bool Filter::match(const FrameInfo &fi, const LeviInfo &li) const {
  if (fi.verdict == Verdict::Encrypted && !show_encrypted) {
    return false;
  }
  if (nmacs > 0) {
    if (!fi.has_addrs) {
      return false; // Adressen nicht verfuegbar: kann nicht zugeordnet werden
    }
    bool hit = false;

    for (int i = 0; i < nmacs; i++) {
      if (memcmp(macs[i], fi.src, 6) == 0 || memcmp(macs[i], fi.dst, 6) == 0) {
        hit = true;
      }
    }
    if (!hit) {
      return false;
    }
  }
  bool is_levi = li.kind != LeviInfo::Kind::NotLevi;

  if (levi_only && !is_levi) {
    return false;
  }
  if (type_active()) {
    // nur Rahmen mit bestaetigtem Kopf (CRC ok): ungepruefte Typfelder wuerden den Filter verfaelschen
    return li.hdr_verified && has_type(li.type);
  }
  return true;
}

void Filter::describe(Out &out) const {
  out.puts("Filter:\n");
  out.puts("  MAC (Absender oder Ziel): ");

  if (nmacs == 0) {
    out.puts("alle\n");
  } else {
    for (int i = 0; i < nmacs; i++) {
      char s[18];
      mac_to_str(macs[i], s, sizeof(s));
      out.printf("%s%s", i ? ", " : "", s);
    }
    out.puts("\n");
  }
  out.puts("  LeviLuma-Typ (nur Rahmen mit gueltiger CRC): ");

  if (!type_active()) {
    out.puts("alle\n");
  } else {
    bool first = true;

    for (int t = 0; t < 256; t++) {
      if (has_type((uint8_t)t)) {
        char lab[40];
        type_label((uint8_t)t, lab, sizeof(lab));
        out.printf("%s%s", first ? "" : ", ", lab);
        first = false;
      }
    }
    out.puts("\n");
  }
  out.printf("  nur LeviLuma-Kennung: %s, verschluesselte Rahmen anzeigen: %s\n", levi_only ? "ja" : "nein", show_encrypted ? "ja" : "nein");
}

// ---- Dedup ----

uint32_t Dedup::key(const FrameInfo &fi) {
  uint32_t h = 2166136261u; // FNV-1a
  auto mix = [&h](const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
      h = (h ^ p[i]) * 16777619u;
    }
  };
  mix(fi.src, 6);
  mix(fi.dst, 6);
  mix(fi.body, fi.body_len);
  h = (h ^ (uint32_t)fi.body_len) * 16777619u;
  return h;
}

bool Dedup::seen(Source src, const FrameInfo &fi, uint32_t t_ms) {
  if (!fi.has_addrs || fi.verdict != Verdict::Ok) {
    return false; // nur lesbare Rahmen mit Adressen sind vergleichbar
  }
  uint32_t k = key(fi);

  for (int i = 0; i < N; i++) {
    E &e = e_[i];

    if (e.used && !e.matched && e.key == k && e.src != src && (uint32_t)(t_ms - e.t_ms) <= window_ms_) {
      e.matched = true; // jeder Eintrag erklaert hoechstens ein Duplikat
      return true;
    }
  }
  E &slot = e_[next_];
  next_ = (next_ + 1) % N;
  slot.used = true;
  slot.matched = false;
  slot.key = k;
  slot.t_ms = t_ms;
  slot.src = src;
  return false;
}

} // namespace dbg
