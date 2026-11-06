#include "dbg_format.h"
#include <stdio.h>
#include <string.h>

namespace dbg {

static const char *NA = "nicht verfuegbar";

static const char *dest_kind(const FrameInfo &fi) {
  if (!fi.has_addrs) {
    return NA;
  }
  if (fi.broadcast || mac_is_broadcast(fi.dst)) {
    return "Broadcast";
  }
  if (fi.dst[0] & 1) {
    return "Multicast";
  }
  return "Unicast";
}

void format_record(Out &out, uint32_t index, const Capture &c, const FrameInfo &fi, const LeviInfo &li, const FormatOptions &opt) {
  char a[40], b[40];

  // Kopfzeile
  out.printf("#%lu  t=%lu.%03lus  Kanal ", (unsigned long)index, (unsigned long)(c.t_ms / 1000), (unsigned long)(c.t_ms % 1000));
  if (c.channel) {
    out.printf("%u", c.channel);
  } else {
    out.puts(NA);
  }
  out.puts("  RSSI ");
  if (c.has_rssi) {
    out.printf("%d dBm", c.rssi);
  } else {
    out.puts(NA);
  }
  out.printf("  Weg: %s", source_name(c.source));
  if (c.has_rx_us) {
    out.printf("  (HW-Zeit %lu us)", (unsigned long)c.rx_us);
  }
  out.puts("\n");

  // Adressen
  if (fi.has_addrs) {
    mac_to_str(fi.src, a, sizeof(a));
    mac_to_str(fi.dst, b, sizeof(b));
    out.printf("  Absender   : %s\n  Ziel       : %s  (%s)\n", a, b, dest_kind(fi));
  } else {
    out.printf("  Absender   : %s\n  Ziel       : %s\n", NA, NA);
  }

  // 802.11
  if (fi.has_mac_header) {
    out.printf("  802.11     : Typ %u Subtyp %u%s, Retry %s, Protected %s, ToDS/FromDS %u/%u, Rahmenzaehler %u Fragment %u\n", fi.fc_type, fi.fc_subtype,
               (fi.fc_type == 0 && fi.fc_subtype == 13) ? " (Action)" : ((fi.fc_type == 0 && fi.fc_subtype == 14) ? " (Action No Ack)" : ""),
               fi.retry ? "ja" : "nein", fi.protected_bit ? "ja" : "nein", fi.to_ds, fi.from_ds, fi.seq_ctrl >> 4, fi.seq_ctrl & 0xF);
    mac_to_str(fi.addr3, a, sizeof(a));
    out.printf("  Adressfeld3: %s\n", a);
  } else {
    out.printf("  802.11     : %s (normaler ESP-NOW-Empfang liefert keinen MAC-Kopf)\n", NA);
  }

  // Laengen
  out.puts("  Laengen    : ");
  if (fi.has_mac_header) {
    out.printf("Rahmen laut Treiber %u Byte", (unsigned)fi.frame_len_driver);
    if (c.fcs_included) {
      out.printf(" (inkl. 4 Byte FCS, ohne FCS %u)", (unsigned)fi.frame_len_no_fcs);
    }
    out.printf(", erfasst %u, MAC-Kopf %u", (unsigned)fi.captured_len, (unsigned)fi.mac_hdr_len);
    if (fi.has_element) {
      out.printf(", ESP-NOW-Element %u (Nutzdaten laut Element %u)", fi.element_len, (unsigned)fi.body_declared);
    }
  } else {
    out.printf("Rahmen laut Treiber %s, Nutzdaten laut Treiber %u Byte, erfasst %u", NA, (unsigned)c.orig_len, (unsigned)fi.captured_len);
  }
  out.puts("\n");

  // ESP-NOW-Rahmeninformationen
  if (fi.has_mac_header && fi.has_category) {
    out.printf("  ESP-NOW    : Kategorie %u, OUI 18:FE:34, Zufallswerte %02X%02X%02X%02X", ESPNOW_CATEGORY, fi.random[0], fi.random[1], fi.random[2], fi.random[3]);
    if (fi.has_element) {
      out.printf(", Element-ID %u, Typ %u, Versionsfeld 0x%02X (untere 4 Bit: %u)", ESPNOW_ELEMENT_ID, ESPNOW_ELEMENT_TYPE, fi.element_version_raw,
                 fi.element_version_raw & 0x0F);
    }
    if (fi.espnow_elements > 1) {
      out.printf(", %d ESP-NOW-Elemente", fi.espnow_elements);
    }
    out.puts("\n");
  } else if (fi.has_mac_header) {
    out.printf("  ESP-NOW    : Kopf nicht lesbar (%s)\n", fi.protected_bit ? "verschluesselt" : NA);
  } else {
    out.printf("  ESP-NOW    : Versions-/Verschluesselungsangaben %s (nur im Rahmen, nicht im Callback)\n", NA);
  }

  // Bewertung
  out.printf("  Bewertung  : %s - %s\n", verdict_name(fi.verdict), fi.reason);

  // Nutzdaten
  const uint8_t *body = fi.body;
  size_t blen = fi.body_len;
  bool dump_full = opt.full_frame;

  if (fi.verdict == Verdict::Encrypted) {
    out.printf("  LeviLuma   : NICHT DEKODIERT - Inhalt verschluesselt (Schluessel liegt dem Diagnosegeraet nicht vor)\n");
    out.printf("  Rohdaten   : %u Byte nach dem MAC-Kopf, verschluesselt (nur Chiffretext, keine Klartextfelder):\n", (unsigned)blen);
    hexdump(out, "    ", body, blen, opt.ascii);
  } else if (body && (fi.verdict == Verdict::Ok || fi.verdict == Verdict::Truncated)) {
    out.printf("  Nutzdaten  : %u Byte vorhanden", (unsigned)blen);
    if (fi.body_declared_known && fi.body_declared != blen) {
      out.printf(" (angekuendigt %u)", (unsigned)fi.body_declared);
    }
    out.puts("\n");

    // LeviLuma
    if (li.kind == LeviInfo::Kind::NotLevi) {
      out.printf("  LeviLuma   : kein LeviLuma-Rahmen (%s)\n", li.reason);
    } else {
      char lab[40], fl[64];
      if (li.hdr_read) {
        type_label(li.type, lab, sizeof(lab));
        flags_label(li.flags, fl, sizeof(fl));
        out.printf("  LeviLuma   : %s%s\n", li.kind == LeviInfo::Kind::Decoded ? "GUELTIG" : "FEHLER", li.hdr_verified ? "" : " (Kopffelder UNGEPRUEFT)");
        out.printf("    Version  : %u%s\n", li.version, li.version == LVP_VERSION ? "" : (li.hdr_verified ? "  (vom Protokoll nicht unterstuetzt)" : ""));
        out.printf("    Typ      : %s\n", lab);
        out.printf("    Flags    : %s\n", fl);
        out.printf("    Sequenz  : %u   Referenzsequenz: %u   Sitzung: 0x%08lX\n", li.seq, li.ref_seq, (unsigned long)li.session);
        out.printf("    Laenge   : len-Feld %u, vorhanden %u\n", li.len, (unsigned)li.payload_avail);
        if (li.crc_checked) {
          out.printf("    CRC      : %s (Rahmen 0x%04X, berechnet 0x%04X)\n", li.crc_ok ? "OK" : "FALSCH", li.crc_rx, li.crc_calc);
        } else {
          out.printf("    CRC      : nicht geprueft (Rahmen 0x%04X; vollstaendige Nutzdaten fehlen)\n", li.crc_rx);
        }
      } else {
        out.puts("  LeviLuma   : FEHLER - Kopf nicht lesbar\n");
      }
      if (li.kind != LeviInfo::Kind::Decoded) {
        out.printf("    Fehler   : %s [%s]\n", li.reason, lvp_err_name((uint8_t)li.err));
      }
      if (li.kind == LeviInfo::Kind::Decoded) {
        out.puts("    Felder   :\n");
        for (int i = 0; i < li.fields.n; i++) {
          out.printf("      %-16s = %s\n", li.fields.f[i].name, li.fields.f[i].value);
        }
        if (li.fields.overflow) {
          out.puts("      (weitere Felder nicht angezeigt)\n");
        }
      }
    }
    out.printf("  Hexdump    : Nutzdaten (ESP-NOW-Body), %u Byte\n", (unsigned)blen);
    hexdump(out, "    ", body, blen, opt.ascii);
  } else {
    // nicht auswertbar: alles Erfasste zeigen
    dump_full = true;
    out.puts("  LeviLuma   : nicht ausgewertet (Rahmen nicht als lesbarer ESP-NOW-Rahmen erkannt)\n");
  }

  if (dump_full && c.len > 0) {
    out.printf("  Rohrahmen  : %u Byte erfasst%s\n", (unsigned)c.len, c.len < c.orig_len ? " (abgeschnitten)" : "");
    hexdump(out, "    ", c.data, c.len, opt.ascii);
  }
}

} // namespace dbg
