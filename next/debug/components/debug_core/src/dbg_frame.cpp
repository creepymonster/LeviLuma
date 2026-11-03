#include "dbg_frame.h"
#include <string.h>
#include "dbg_text.h"

namespace dbg {

static uint16_t rd16(const uint8_t *p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}

static bool is_oui(const uint8_t *p) {
  return memcmp(p, ESPNOW_OUI, 3) == 0;
}

// Management-Rahmen mit Subtyp Action (13) oder Action No Ack (14)
static bool is_action(uint8_t fc0) {
  uint8_t type = (fc0 >> 2) & 3, subtype = fc0 >> 4;
  return type == 0 && (subtype == 13 || subtype == 14);
}

bool is_espnow_candidate(const uint8_t *f, size_t len) {
  if (!f || len < MAC_HDR_LEN + 4 || !is_action(f[0])) {
    return false;
  }
  bool prot = (f[1] & 0x40) != 0;

  if (prot) {
    // Inhalt verschluesselt: nur der Adressfeld-3-Broadcast (ESP-NOW-Besonderheit laut Doku) bleibt als Hinweis
    return mac_is_broadcast(f + 16);
  }
  size_t hdr = MAC_HDR_LEN + ((f[1] & 0x80) ? 4 : 0);
  return len >= hdr + 4 && f[hdr] == ESPNOW_CATEGORY && is_oui(f + hdr + 1);
}

const char *verdict_name(Verdict v) {
  switch (v) {
  case Verdict::Ok:
    return "OK";
  case Verdict::Encrypted:
    return "VERSCHLUESSELT";
  case Verdict::NotEspNow:
    return "KEIN ESP-NOW";
  case Verdict::Truncated:
    return "ABGESCHNITTEN";
  case Verdict::Malformed:
    return "BESCHAEDIGT";
  case Verdict::TooShort:
    return "ZU KURZ";
  }
  return "?";
}

const char *source_name(Source s) {
  return s == Source::Promiscuous ? "Promiscuous" : "ESP-NOW-Callback";
}

static void set(FrameInfo &fi, Verdict v, const char *reason) {
  fi.verdict = v;
  fi.reason = reason;
}

static void parse_payload_only(const Capture &c, FrameInfo &fi) {
  fi.has_mac_header = false;
  fi.captured_len = c.len;
  fi.frame_len_driver = c.orig_len;
  fi.frame_len_no_fcs = c.orig_len;
  fi.body = c.data;
  fi.body_len = c.len;
  fi.body_declared = c.orig_len;
  fi.body_declared_known = true;

  if (c.has_addr) {
    memcpy(fi.src, c.src, 6);
    memcpy(fi.dst, c.dst, 6);
    fi.has_addrs = true;
  }
  fi.body_complete = c.len == c.orig_len;

  if (fi.body_complete) {
    set(fi, Verdict::Ok, "Nutzdaten aus dem ESP-NOW-Callback (ohne 802.11-Kopf)");
  } else {
    set(fi, Verdict::Truncated, "Erfassung kuerzer als die vom Treiber gemeldete Laenge");
  }
}

void parse_capture(const Capture &c, FrameInfo &fi) {
  fi = FrameInfo();

  if (c.len > MAX_CAPTURE) {
    // darf nicht vorkommen (Callback begrenzt); lieber ablehnen als ueber das Feld hinaus lesen
    set(fi, Verdict::Malformed, "interner Fehler: Erfassungslaenge ueber Puffer");
    return;
  }
  if (c.source == Source::EspNowCallback) {
    parse_payload_only(c, fi);
    return;
  }
  const uint8_t *d = c.data;
  size_t n = c.len;
  fi.has_mac_header = true;
  fi.captured_len = n;
  fi.frame_len_driver = c.orig_len;
  fi.frame_len_no_fcs = (c.fcs_included && c.orig_len >= 4) ? (size_t)c.orig_len - 4 : c.orig_len;
  bool cut = n < c.orig_len; // Erfassung kuerzer als der Rahmen
  // Bytes, die zum Rahmen ohne FCS gehoeren und erfasst wurden
  size_t avail = n < fi.frame_len_no_fcs ? n : fi.frame_len_no_fcs;

  if (c.fcs_included && !cut && c.orig_len >= 4 && n == c.orig_len) {
    fi.fcs_present = true;
    memcpy(fi.fcs, d + n - 4, 4);
  }
  if (avail < MAC_HDR_LEN) {
    set(fi, Verdict::TooShort, "kuerzer als der 802.11-MAC-Kopf (24 Byte)");
    return;
  }
  fi.frame_control = rd16(d);
  fi.fc_type = (d[0] >> 2) & 3;
  fi.fc_subtype = d[0] >> 4;
  fi.to_ds = (d[1] & 0x01) != 0;
  fi.from_ds = (d[1] & 0x02) != 0;
  fi.retry = (d[1] & 0x08) != 0;
  fi.protected_bit = (d[1] & 0x40) != 0;
  memcpy(fi.dst, d + 4, 6);
  memcpy(fi.src, d + 10, 6);
  memcpy(fi.addr3, d + 16, 6);
  fi.has_addrs = true;
  fi.broadcast = mac_is_broadcast(fi.dst);
  fi.multicast = !fi.broadcast && (fi.dst[0] & 1);
  fi.seq_ctrl = rd16(d + 22);
  fi.mac_hdr_len = MAC_HDR_LEN + ((d[1] & 0x80) ? 4 : 0);

  if (!is_action(d[0])) {
    set(fi, Verdict::NotEspNow, "kein Action-Rahmen (ESP-NOW nutzt Management-Action-Rahmen)");
    return;
  }
  if (avail < fi.mac_hdr_len) {
    set(fi, Verdict::TooShort, "kuerzer als der MAC-Kopf mit HT-Control");
    return;
  }
  if (fi.protected_bit) {
    fi.body = d + fi.mac_hdr_len;
    fi.body_len = avail - fi.mac_hdr_len;
    fi.body_complete = !cut;

    if (mac_is_broadcast(fi.addr3)) {
      set(fi, Verdict::Encrypted,
          "Protected-Bit gesetzt, Inhalt verschluesselt und ohne Schluessel nicht lesbar (Zuordnung zu ESP-NOW ist nur eine Heuristik: Action-Rahmen mit Broadcast im Adressfeld 3)");
    } else {
      set(fi, Verdict::NotEspNow, "geschuetzter Action-Rahmen ohne Broadcast im Adressfeld 3 (nicht als ESP-NOW erkennbar)");
    }
    return;
  }
  // Kopf nach dem MAC-Kopf: Kategorie, OUI, Zufallswerte
  size_t off = fi.mac_hdr_len;

  if (avail < off + 1) {
    set(fi, cut ? Verdict::Truncated : Verdict::TooShort, "keine Bytes nach dem MAC-Kopf");
    return;
  }
  fi.has_category = true;

  if (d[off] != ESPNOW_CATEGORY) {
    set(fi, Verdict::NotEspNow, "Action-Kategorie ist nicht 127 (herstellerspezifisch)");
    return;
  }
  if (avail < off + 4 || !is_oui(d + off + 1)) {
    set(fi, (avail < off + 4) ? (cut ? Verdict::Truncated : Verdict::TooShort) : Verdict::NotEspNow,
        (avail < off + 4) ? "Rahmen endet in der Hersteller-Kennung" : "Hersteller-Kennung (OUI) ist nicht 18:FE:34");
    return;
  }
  if (avail < off + 8) {
    set(fi, cut ? Verdict::Truncated : Verdict::TooShort, "Rahmen endet in den Zufallswerten");
    return;
  }
  memcpy(fi.random, d + off + 4, 4);
  off += 8;

  // Vendor-Elemente durchlaufen
  while (off < avail) {
    if (avail - off < 2) {
      set(fi, cut ? Verdict::Truncated : Verdict::Malformed, "einzelnes Restbyte hinter dem letzten Element");
      return;
    }
    uint8_t id = d[off], l = d[off + 1];
    size_t end = off + 2 + (size_t)l;
    fi.element_count++;
    bool is_en = id == ESPNOW_ELEMENT_ID && l >= 5 && end <= avail && is_oui(d + off + 2) && d[off + 5] == ESPNOW_ELEMENT_TYPE;

    // Element laenger als der erfasste Rest
    if (end > avail) {
      bool looks_en = id == ESPNOW_ELEMENT_ID && l >= 5 && avail >= off + 6 && is_oui(d + off + 2) && d[off + 5] == ESPNOW_ELEMENT_TYPE;

      if (looks_en && !fi.has_element) {
        fi.espnow_elements++;
        fi.has_element = true;
        fi.element_len = l;

        if (avail >= off + 7) {
          fi.element_version_raw = d[off + 6];
          fi.body = d + off + 7;
          fi.body_len = avail - (off + 7);
        } else {
          fi.body = d + off + 6; // Versions-Byte fehlt
          fi.body_len = 0;
        }
        fi.body_declared = (size_t)l - 5;
        fi.body_declared_known = true;
      }
      const char *why = !cut ? "Elementlaenge ueberschreitet den Rahmen"
                        : (fi.has_element && fi.body_complete ? "weiteres Element abgeschnitten (Erfassung kuerzer als der Rahmen); erstes ESP-NOW-Element vollstaendig"
                                                              : "Erfassung endet im ESP-NOW-Element (Rahmen laenger als der Erfassungspuffer)");
      set(fi, cut ? Verdict::Truncated : Verdict::Malformed, why);
      return;
    }
    if (is_en) {
      fi.espnow_elements++;

      if (!fi.has_element) {
        fi.has_element = true;
        fi.element_len = l;
        fi.element_version_raw = d[off + 6];
        fi.body = d + off + 7;
        fi.body_len = (size_t)l - 5;
        fi.body_declared = fi.body_len;
        fi.body_declared_known = true;
        fi.body_complete = true;
      }
    } else if (id == ESPNOW_ELEMENT_ID && l < 5) {
      set(fi, Verdict::Malformed, "ESP-NOW-Element kuerzer als sein fester Kopf (5 Byte)");
      return;
    }
    off = end;
  }
  if (!fi.has_element) {
    set(fi, Verdict::Malformed, "Kategorie 127 mit Espressif-OUI, aber kein ESP-NOW-Element (ID 221, Typ 4)");
    return;
  }
  if (fi.espnow_elements > 1) {
    set(fi, Verdict::Ok, "mehrere ESP-NOW-Elemente im Rahmen; ausgewertet wird nur das erste");
  } else {
    set(fi, Verdict::Ok, "ESP-NOW-Element vollstaendig");
  }
}

} // namespace dbg
