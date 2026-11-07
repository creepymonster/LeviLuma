# Testauswertung LeviLuma Next

> **Automatisch erzeugt – nicht von Hand ändern.** Erzeugt von `tests/tools/gen_report.py` aus den Ergebnissen des abschließenden CI-Laufs (`.github/workflows/tests.yml`, Job *report*). Lokale Testläufe und Entwicklungsschritte ändern diese Datei nicht.

## Gesamtergebnis (Host-Tests)

**BESTANDEN** – Alle ausgeführten Host-Tests bestanden.

| Kennzahl | Wert |
|---|---|
| Tests ausgeführt | 89 |
| – bestanden | 89 |
| – fehlgeschlagen | 0 |
| – übersprungen (nicht ausgeführt) | 0 |
| Assertions (Prüfungen) ausgeführt | 10570 |
| – davon fehlgeschlagen | 0 |
| Eigene Summenzeile des Testprogramms | 89 Tests, 10570 Assertions, 0 Fehler |

## Getesteter Stand

| | |
|---|---|
| Getesteter Commit | `6654050fad43d80760bb80d5be28db6a871c6e7a` |
| Ref | `refs/heads/main` (Auslöser: push) |
| Testlauf gestartet | 2026-11-07 16:31:26 UTC (2026-11-07 17:31:26 CET) |
| CI-Lauf | https://github.com/creepymonster/LeviLuma/actions/runs/42391681047 |

Der Bericht beschreibt den oben genannten Commit. Seine Übernahme in `tests/README.md` erfolgt in einem **separaten Folge-Commit** (Bot, `[skip ci]`), der nur diese Datei ändert; der Quellstand ist deshalb der genannte Commit, nicht der Berichts-Commit.

## Testumgebung und Prüfverfahren

| | |
|---|---|
| Betriebssystem | Linux-7.0.0-1017-azure-x86_64-with-glibc2.39, Runner-Image 20261101.360.1 |
| CMake | cmake version 3.31.6 |
| Compiler (g++) | 13.3.0 |
| Sanitizer (im Testprogramm aktiv) | AddressSanitizer,UndefinedBehaviorSanitizer (no-recover) |
| Compiler-Warnungen | `-Wall -Wextra -Werror` (Warnung = Build-Fehler) |
| Schritt *Build* | success |
| Schritt *Testlauf* | success |
| Schritt *Legacy-Kopien byte-gleich* (`cmp`) | success |

Die Host-Tests laufen ohne Funk-, BLE- und Display-Hardware gegen simulierte Gegenstellen.

## Host-Tests nach Bereichen

### Protokoll (9/9 bestanden)

_ESP-NOW-Rahmen: Kodierung, Pruefsumme, Grenzen, Fehlerfaelle beim Dekodieren, Validierung, Sequenz-Ueberlauf, Fuzzing._

| Test | Ergebnis | Assertions | Abdeckung |
|---|---|---|---|
| `test_crc_reference` | bestanden | 1 |  |
| `test_header_layout_little_endian` | bestanden | 14 |  |
| `test_roundtrip_all_types` | bestanden | 53 |  |
| `test_frame_size_limit` | bestanden | 5 |  |
| `test_name_truncated_and_sanitized` | bestanden | 6 |  |
| `test_decode_errors` | bestanden | 10 |  |
| `test_set_state_validation` | bestanden | 9 |  |
| `test_seq_wrap` | bestanden | 6 |  |
| `test_fuzz_no_crash` | bestanden | 1 |  |

### Zuverlaessigkeit (6/6 bestanden)

_Wiederholung mit Zeitablauf, Zeitueberlauf, Duplikaterkennung beim Empfaenger, Online-Erkennung._

| Test | Ergebnis | Assertions | Abdeckung |
|---|---|---|---|
| `test_txq_retry_and_timeout` | bestanden | 11 |  |
| `test_txq_complete_and_cancel` | bestanden | 19 |  |
| `test_txq_time_wrap` | bestanden | 2 |  |
| `test_dedup` | bestanden | 9 |  |
| `test_dedup_eviction` | bestanden | 7 |  |
| `test_online` | bestanden | 4 |  |

### Gateway (20/20 bestanden)

_Lichtzustand, BLE-Befehlsreihenfolge, Kalibrierung und Speicher, Kopplungstabelle, Schluessel, Gateway-Handler (Anfragen, Antworten, Ablehnungen)._

| Test | Ergebnis | Assertions | Abdeckung |
|---|---|---|---|
| `test_light_model_semantics` | bestanden | 20 |  |
| `test_scheduler_order_after_connect` | bestanden | 10 |  |
| `test_scheduler_packets_and_brightness_scale` | bestanden | 6 |  |
| `test_scheduler_color_calibrated_and_raw_throttle` | bestanden | 9 |  |
| `test_scheduler_music_eq_and_power_off` | bestanden | 7 |  |
| `test_scheduler_fade` | bestanden | 7 |  |
| `test_calib_store_persistence` | bestanden | 18 |  |
| `test_calib_defaults_points` | bestanden | 1 |  |
| `test_calib_matches_legacy_core` | bestanden | 4 |  |
| `test_pairing_table` | bestanden | 21 |  |
| `test_band_memory_choose_and_persist` | bestanden | 27 | Gemerktes Band: Auswahl (staerkstes, gemerktes, fehlendes), Speichern, Laden, Vergessen |
| `test_band_memory_storage_errors` | bestanden | 9 | Gemerktes Band: Speicherfehler behalten alten Stand, beschaedigte Daten werden verworfen |
| `test_key_hex` | bestanden | 9 |  |
| `test_handler_discovery_and_pairing_window` | bestanden | 19 |  |
| `test_handler_key_allows_pairing_anytime` | bestanden | 5 |  |
| `test_handler_set_state_confirmed_and_dedup` | bestanden | 16 |  |
| `test_handler_calibration_roundtrip` | bestanden | 16 |  |
| `test_handler_unpair_and_reconnect` | bestanden | 10 |  |
| `test_handler_rejects_garbage_and_responses` | bestanden | 4 |  |
| `test_state_notify_frame` | bestanden | 4 |  |

### Controller (30/30 bestanden)

_Gateway-Register, Zigbee-Zuordnung, Touch-Kalibrierung und End-to-End Controller<->Gateway ueber simuliertes ESP-NOW (echter Gateway-Code): Kopplung, Verlust, Wiederholung, Warteregel, Neustart, Kalibrierung._

| Test | Ergebnis | Assertions | Abdeckung |
|---|---|---|---|
| `test_calib_targets_match_core` | bestanden | 31 |  |
| `test_registry_persistence` | bestanden | 34 |  |
| `test_registry_paired_limit` | bestanden | 11 |  |
| `test_zb_endpoint_mapping_stable` | bestanden | 18 |  |
| `test_zb_conversions` | bestanden | 115 |  |
| `test_touch_cal` | bestanden | 7 |  |
| `test_e2e_discovery_pair_control` | bestanden | 18 |  |
| `test_e2e_key_required` | bestanden | 13 |  |
| `test_e2e_pairing_window_closed` | bestanden | 2 |  |
| `test_e2e_retry_on_loss` | bestanden | 4 |  |
| `test_e2e_pair_busy_while_pending` | bestanden | 6 | Zweiter Kopplungsversuch waehrend einer laufenden Kopplung wird abgewiesen, kein zweiter PAIR_REQ |
| `test_e2e_trace_pair_retry_and_timeout` | bestanden | 19 | Diagnose meldet Versuche mit Wartezeit, Antwort und endgueltigen Timeout |
| `test_e2e_coalescing_latest_wins` | bestanden | 4 | Viele Befehle waehrend einer offenen Anfrage: nur erster (unterwegs) und letzter (wartend) erreichen das Gateway |
| `test_e2e_waiting_command_replaced_not_merged` | bestanden | 12 | A unterwegs, B/C/D folgen: ausschliesslich D; keine Felder aelterer Befehle uebernommen |
| `test_e2e_loss_then_query_does_not_overtake` | bestanden | 14 | SET_STATE geht verloren, GET_CALIB/GET_STATE ueberholen die Wiederholung nicht (kein STALE) |
| `test_e2e_query_demand_coalesced` | bestanden | 3 | Abfragebedarf wird zusammengefasst, kein Rueckstau |
| `test_e2e_final_timeout_then_last_waiting` | bestanden | 8 | Endgueltiger Timeout: danach nur der letzte wartende Befehl, der alte kehrt nie zurueck |
| `test_e2e_gateways_independent_queues` | bestanden | 9 | Zwei Gateways werden unabhaengig behandelt |
| `test_e2e_calib_save_then_restore_color` | bestanden | 16 | Kalibrierung speichern wird von der Farbwiederherstellung nicht verdraengt |
| `test_e2e_calib_save_timeout_reported` | bestanden | 6 | Timeout beim Speichern der Kalibrierung wird gemeldet, nicht als Erfolg |
| `test_e2e_waiting_music_then_color_music_stays_off` | bestanden | 9 | Wartendes 'Musik an' + kalibrierte Farbe: Musik bleibt aus |
| `test_e2e_waiting_color_then_brightness_only_brightness` | bestanden | 4 | Wartende Farbe + Helligkeit: nur Helligkeit |
| `test_e2e_waiting_raw_calibrated_switch_last_wins` | bestanden | 12 | Wechsel roh/kalibriert: letzter wartender Befehl unveraendert |
| `test_e2e_loss_keeps_waiting_command_identical` | bestanden | 12 | Verlust/Wiederholung aendert Inhalt des wartenden Befehls nicht |
| `test_e2e_single_composite_set_state_semantics` | bestanden | 8 | Einzelner SET_STATE mit mehreren Feldern behaelt seine Semantik |
| `test_e2e_offline_and_resync` | bestanden | 8 |  |
| `test_e2e_notify_from_gateway` | bestanden | 2 |  |
| `test_e2e_calibration` | bestanden | 8 |  |
| `test_e2e_unpair` | bestanden | 4 |  |
| `test_e2e_multiple_gateways` | bestanden | 6 |  |

### Runder Controller (2/2 bestanden)

_Ein-Gateway-Regel und Sendedrossel des runden Controllers._

| Test | Ergebnis | Assertions | Abdeckung |
|---|---|---|---|
| `test_round_single_gateway` | bestanden | 9 |  |
| `test_round_throttle` | bestanden | 9 |  |

### Diagnose (22/22 bestanden)

_ESP-NOW-Diagnosegeraet (debug/): Rahmenauswertung, LeviLuma-Dekodierung aller Nachrichtentypen, Ausgabe, Filter, Doppelerkennung, Monitorbefehle, Fuzzing. Alle Rahmen sind synthetisch._

| Test | Ergebnis | Assertions | Abdeckung |
|---|---|---|---|
| `test_dbg_frame_valid_broadcast` | bestanden | 31 | Gueltiger Broadcast-Rahmen (synthetisch): Adressen, Laengen, ESP-NOW-Kopf, LeviLuma-Felder, CRC. |
| `test_dbg_frame_unicast_multicast_retry` | bestanden | 5 | Unicast, Multicast, Retry-Bit (synthetisch). |
| `test_dbg_frame_encrypted_not_decoded` | bestanden | 13 | Verschluesselter Rahmen (Protected-Bit, synthetisch): nur Metadaten und Rohdaten, nie dekodiert. |
| `test_dbg_frame_candidate_prefilter` | bestanden | 8 | Vorfilter im Callback: Action/Action-No-Ack, Kategorie 127, Beacon und kurze Rahmen. |
| `test_dbg_frame_not_espnow` | bestanden | 6 | Fremde Rahmen (Beacon, andere Kategorie, andere OUI) werden nicht als ESP-NOW ausgewertet. |
| `test_dbg_frame_too_short_and_malformed` | bestanden | 22 | Zu kurze und beschaedigte Rahmen (Elementlaenge, fehlendes Element, falsche Treiberlaenge) ohne Absturz. |
| `test_dbg_frame_capture_truncated` | bestanden | 15 | Abgeschnittene Erfassung: Urteil, keine CRC-Aussage, ungepruefte Kopffelder gekennzeichnet; mehrere Elemente. |
| `test_dbg_frame_multi_element_and_version` | bestanden | 7 | Mehrere ESP-NOW-Elemente und rohes Versionsfeld (nicht gedeutet). |
| `test_dbg_frame_payload_only_path` | bestanden | 10 | Normaler ESP-NOW-Empfang (nur Nutzdaten): fehlende Angaben als nicht verfuegbar. |
| `test_dbg_levi_errors` | bestanden | 29 | LeviLuma-Fehlerfaelle: CRC, unbekannte Version/Typ, falsche Nutzdaten, Laenge, kurzer Kopf, Fremddaten. |
| `test_dbg_levi_flags` | bestanden | 5 | Flag-Beschriftung inkl. unbekannter Bits und kleinem Puffer. |
| `test_dbg_levi_all_protocol_types` | bestanden | 394 | Jeder Nachrichtentyp des gemeinsamen Protokolls hat Testrahmen und Dekodierung (faengt neue Typen). |
| `test_dbg_levi_field_values` | bestanden | 26 | Feldnamen und Werte (SET_STATE, STATE, CALIB, NACK, PAIR_RSP, DISCOVER). |
| `test_dbg_format_record` | bestanden | 20 | Ausgabeformat einer Nachricht, Hexdump, ASCII-Spalte, Rohrahmen. |
| `test_dbg_out_truncation` | bestanden | 5 | Ausgabepuffer: Abschneiden ohne Ueberlauf. |
| `test_dbg_hexdump_and_mac` | bestanden | 11 | Hexdump und MAC-Parser. |
| `test_dbg_filter` | bestanden | 28 | Filter nach MAC, LeviLuma-Typ, Kennung, Verschluesselung. |
| `test_dbg_dedup` | bestanden | 16 | Doppelerkennung zwischen zwei Empfangswegen (nie bei gleichem Weg). |
| `test_dbg_commands` | bestanden | 60 | Monitorbefehle: Hilfe, Kanal, Suchlauf, Filter, Anzeige, Statistik, Eingabegrenzen. |
| `test_dbg_stats_format` | bestanden | 5 | Statistikausgabe inkl. Verlustwarnung. |
| `test_dbg_channel_plan` | bestanden | 9 | Kanalplan: zulaessiger Bereich und Umlauf. |
| `test_dbg_fuzz_no_crash` | bestanden | 9025 | Fuzzing (20000 Faelle) von Rahmenauswertung, Dekodierung und Ausgabe unter ASan/UBSan. |

## Fehlgeschlagene, übersprungene und nicht ausgeführte Prüfungen

Keine.

## Firmware-Builds (getrennt von den Host-Tests)

Gebaut von `.github/workflows/next.yml` (ESP-IDF in Container). Der Status wird für denselben Commit abgefragt; ein Build ist kein Test.

| Job | Status | Ergebnis |
|---|---|---|
| ESP-IDF Diagnose ESP-NOW (esp32s3) | completed | erfolgreich |
| ESP-IDF Round Controller (esp32s3) | completed | erfolgreich |
| ESP-IDF Gateway (esp32s3) | completed | erfolgreich |
| ESP-IDF Controller ohne Zigbee (esp32c5) | completed | erfolgreich |
| ESP-IDF Controller mit Zigbee (esp32c5) | completed | erfolgreich |
| ESP-IDF Diagnose ESP-NOW, alle Optionen (esp32s3) | completed | erfolgreich |

## Hardwaretests

**Nicht ausgeführt.** Weder Gateway, Controller noch Diagnosegerät werden in der CI auf Hardware geflasht oder getestet; kein Punkt hier gilt als bestanden. Offene Hardwaretests laut `README.md` (Abschnitt „Offene Hardwaretests“, automatisch übernommen):

1. Display: Ausrichtung/Spiegelung (`LEVILUMA_LCD_ROT_*`), Farben (RGB/BGR, Inversion), SPI-Takt.
2. Touch: Rohbereiche, Drei-Punkt-Kalibrierung, gemeinsamer SPI-Bus mit dem Display unter Last.
3. ESP-NOW zwischen C5 und S3 auf Kanal 1 (C5 Dualband), Reichweite, Wiederholungen.
4. Schlüssel: Kopplung mit richtigem/falschem Schlüssel; ob unverschlüsselte Unicasts eines verschlüsselt eingetragenen Peers verworfen werden.
5. Gateway-BLE (NimBLE-Port): Scan, Verbindung, Schreiben ohne Antwort an 0xFFF3, Wiederverbindung, Koexistenz mit ESP-NOW.
6. Musikmodus und Mikrofon-EQ (schon in Legacy ungetestet; in Next ohne Bedienelement, nur im Protokoll).
7. Zigbee: Beitritt zur FRITZ!Box 5690 Pro, Darstellung mehrerer Endpoints, Farbbefehle (xy/Hue/Farbtemperatur), Koexistenz Zigbee + ESP-NOW auf dem C5.
8. Kalibrierassistent am Band (Vorschau-Drosselung 400 ms).
9. XIAO: LED-Polarität, BOOT-Taste als Kopplungstaste im Betrieb.
