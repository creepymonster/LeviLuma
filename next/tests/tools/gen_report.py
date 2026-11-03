#!/usr/bin/env python3
"""Erzeugt next/tests/README.md aus den tatsaechlichen Ergebnissen der Host-Tests.

Eingaben (alle von der CI geliefert, nichts wird von Hand gepflegt):
  --results   JSON Lines des Testprogramms (tests/test_main.cpp, LEVI_TEST_RESULTS)
  --log       Konsolenausgabe des Testlaufs (nur fuer die Fehlersuche bei Abbruechen)
  --step N=O  Ausgang eines CI-Schritts (success|failure|cancelled|skipped|leer = nicht gelaufen), N: build, run, copies
  --firmware  JSON-Liste der Firmware-Jobs aus next.yml fuer denselben Commit (optional)
Der Generator liefert immer einen Bericht und endet mit 0; der CI-Status wird von den Testschritten bestimmt.
Ereignisse in --results: meta, group, begin, test, summary (siehe test_main.cpp).
"""
import argparse, json, os, platform, re, subprocess, sys
from datetime import datetime, timezone

try:
    from zoneinfo import ZoneInfo
except ImportError:  # pragma: no cover
    ZoneInfo = None


def cell(s):
    return str(s).replace("|", "\\|").replace("\n", " ")


def load(path):
    ev, bad = [], 0
    if path and os.path.exists(path):
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    ev.append(json.loads(line))
                except ValueError:
                    bad += 1  # abgeschnittene letzte Zeile bei hartem Abbruch
    return ev, bad


def tool_version(cmd):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=10).stdout.splitlines()[0].strip()
    except Exception:
        return "unbekannt"


def hardware_list(readme):
    """Offene Hardwaretests unmittelbar aus next/README.md (keine zweite Liste)."""
    try:
        txt = open(readme, encoding="utf-8").read()
    except OSError:
        return None
    m = re.search(r"^### Offene Hardwaretests\s*\n(.*?)(?=^#{1,3} |\Z)", txt, re.S | re.M)
    if not m:
        return None
    return [l.strip() for l in m.group(1).splitlines() if re.match(r"\d+\.", l.strip())]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results")
    ap.add_argument("--log")
    ap.add_argument("--out", required=True)
    ap.add_argument("--commit", default="unbekannt")
    ap.add_argument("--ref", default="unbekannt")
    ap.add_argument("--event", default="unbekannt")
    ap.add_argument("--run-url", default="")
    ap.add_argument("--step", action="append", default=[])
    ap.add_argument("--firmware")
    ap.add_argument("--next-readme", default=os.path.join(os.path.dirname(__file__), "..", "..", "README.md"))
    a = ap.parse_args()
    steps = dict(s.split("=", 1) for s in a.step if "=" in s)
    build, run, copies = (steps.get(k, "") for k in ("build", "run", "copies"))

    ev, bad = load(a.results)
    meta = next((e for e in ev if e.get("ev") == "meta"), None)
    summary = next((e for e in ev if e.get("ev") == "summary"), None)
    groups, order, tests, begun = {}, [], [], None
    for e in ev:
        k = e.get("ev")
        if k == "group":
            groups[e["name"]] = e.get("desc", "")
            order.append(e["name"])
        elif k == "begin":
            begun = e
        elif k == "test":
            tests.append(e)
            begun = None
    aborted = begun if (begun and not summary) else None
    n = lambda st: sum(1 for t in tests if t["status"] == st)
    passed, failed, skipped = n("passed"), n("failed"), n("skipped")
    checks = sum(t.get("checks", 0) for t in tests)
    failures = sum(t.get("failures", 0) for t in tests)

    # Gesamturteil: nur "bestanden", wenn wirklich alles lief und der Testschritt erfolgreich war
    if build and build != "success":
        verdict, why = "NICHT AUSGEFÜHRT", "Build der Tests ist fehlgeschlagen oder wurde abgebrochen (%s)." % build
    elif not meta:
        verdict, why = "NICHT AUSGEFÜHRT", "Keine Testergebnisse vorhanden (Testprogramm nicht gestartet oder Ergebnisdatei fehlt)."
    elif aborted or not summary:
        verdict, why = "ABGEBROCHEN", "Das Testprogramm ist vor dem Ende abgebrochen (Absturz, Sanitizer-Fund oder Zeitüberschreitung); die Zahlen sind unvollständig."
    elif failed or failures or run != "success":
        verdict, why = "FEHLGESCHLAGEN", "Mindestens ein Test oder der Testschritt ist fehlgeschlagen."
    elif copies and copies != "success":
        verdict, why = "FEHLGESCHLAGEN", "Die Prüfung der Legacy-Kopien ist fehlgeschlagen (%s)." % copies
    else:
        verdict, why = "BESTANDEN", "Alle ausgeführten Host-Tests bestanden." + (" %d Test(s) wurden übersprungen." % skipped if skipped else "")

    started = (meta or {}).get("started_utc")
    when = "unbekannt (Tests nicht gestartet)"
    if started:
        dt = datetime.strptime(started, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
        when = dt.strftime("%Y-%m-%d %H:%M:%S UTC")
        if ZoneInfo:
            try:
                when += " (%s)" % dt.astimezone(ZoneInfo("Europe/Berlin")).strftime("%Y-%m-%d %H:%M:%S %Z")
            except Exception:
                pass

    o = []
    w = o.append
    w("# Testauswertung LeviLuma Next\n")
    w("> **Automatisch erzeugt – nicht von Hand ändern.** Erzeugt von `tests/tools/gen_report.py` aus den Ergebnissen des abschließenden CI-Laufs "
      "(`.github/workflows/tests.yml`, Job *report*). Lokale Testläufe und Entwicklungsschritte ändern diese Datei nicht.\n")
    w("## Gesamtergebnis (Host-Tests)\n")
    w("**%s** – %s\n" % (verdict, why))
    w("| Kennzahl | Wert |\n|---|---|")
    w("| Tests ausgeführt | %d |" % (passed + failed))
    w("| – bestanden | %d |" % passed)
    w("| – fehlgeschlagen | %d |" % failed)
    w("| – übersprungen (nicht ausgeführt) | %d |" % skipped)
    w("| Assertions (Prüfungen) ausgeführt | %d |" % checks)
    w("| – davon fehlgeschlagen | %d |" % failures)
    if summary:
        w("| Eigene Summenzeile des Testprogramms | %d Tests, %d Assertions, %d Fehler |" % (summary["tests"], summary["checks"], summary["failures"]))
    w("")
    w("## Getesteter Stand\n")
    w("| | |\n|---|---|")
    w("| Getesteter Commit | `%s` |" % a.commit)
    w("| Ref | `%s` (Auslöser: %s) |" % (a.ref, a.event))
    w("| Testlauf gestartet | %s |" % when)
    if a.run_url:
        w("| CI-Lauf | %s |" % a.run_url)
    w("")
    w("Der Bericht beschreibt den oben genannten Commit. Seine Übernahme in `next/tests/README.md` erfolgt in einem **separaten Folge-Commit** "
      "(Bot, `[skip ci]`), der nur diese Datei ändert; der Quellstand ist deshalb der genannte Commit, nicht der Berichts-Commit.\n")
    w("## Testumgebung und Prüfverfahren\n")
    w("| | |\n|---|---|")
    if meta:
        w("| Betriebssystem | %s%s |" % (platform.platform(), (", Runner-Image " + os.environ["ImageVersion"]) if os.environ.get("ImageVersion") else ""))
        w("| CMake | %s |" % cell(tool_version(["cmake", "--version"])))
    else:
        w("| Betriebssystem / CMake | unbekannt (Tests nicht gestartet) |")
    w("| Compiler (g++) | %s |" % cell((meta or {}).get("compiler", "unbekannt")))
    w("| Sanitizer (im Testprogramm aktiv) | %s |" % cell((meta or {}).get("sanitizers", "unbekannt (Tests nicht gestartet)")))
    w("| Compiler-Warnungen | `-Wall -Wextra -Werror` (Warnung = Build-Fehler) |")
    w("| Schritt *Build* | %s |" % (build or "nicht gelaufen"))
    w("| Schritt *Testlauf* | %s |" % (run or "nicht gelaufen"))
    w("| Schritt *Legacy-Kopien byte-gleich* (`cmp`) | %s |" % (copies or "nicht gelaufen"))
    w("")
    w("Die Host-Tests laufen ohne Funk-, BLE- und Display-Hardware gegen simulierte Gegenstellen.\n")

    w("## Host-Tests nach Bereichen\n")
    if not tests and not aborted:
        w("_Keine Testergebnisse vorhanden – nichts wurde ausgeführt._\n")
    for g in order:
        gt = [t for t in tests if t["group"] == g]
        gp = sum(t["status"] == "passed" for t in gt)
        w("### %s (%d/%d bestanden)\n" % (g, gp, len(gt)))
        if groups.get(g):
            w("_%s_\n" % groups[g])
        w("| Test | Ergebnis | Assertions | Abdeckung |\n|---|---|---|---|")
        for t in gt:
            res = {"passed": "bestanden", "failed": "**fehlgeschlagen**", "skipped": "**übersprungen**"}.get(t["status"], t["status"])
            desc = t.get("desc") or ""
            if t["status"] == "skipped":
                desc = ("Grund: %s" % t.get("reason", "nicht angegeben")) + (". " + desc if desc else "")
            w("| `%s` | %s | %d | %s |" % (t["name"], res, t.get("checks", 0), cell(desc)))
        if aborted and aborted.get("group") == g:
            w("| `%s` | **abgebrochen** | – | Programm endete währenddessen |" % aborted["test"])
        w("")

    problems = [t for t in tests if t["status"] != "passed"]
    w("## Fehlgeschlagene, übersprungene und nicht ausgeführte Prüfungen\n")
    items = []
    if verdict == "NICHT AUSGEFÜHRT":
        items.append("**Alle Host-Tests: nicht ausgeführt.** %s" % why)
    if aborted:
        items.append("**`%s`: abgebrochen.** Folgende Tests wurden nicht mehr erreicht; ihre Anzahl ist unbekannt." % aborted["test"])
    if aborted or (meta and not summary):
        items.append("Ergebnisdatei ohne Abschlusszeile: Lauf unvollständig.")
    if bad:
        items.append("%d unlesbare Zeile(n) in der Ergebnisdatei (abgeschnitten)." % bad)
    for t in problems:
        if t["status"] == "failed":
            items.append("**`%s` fehlgeschlagen** (%d Assertion(s)):\n  " % (t["name"], t.get("failures", 0)) + "\n  ".join("- `%s`" % cell(m) for m in t.get("messages", [])))
        elif t["status"] == "skipped":
            items.append("**`%s` übersprungen:** %s" % (t["name"], t.get("reason", "ohne Begründung")))
    if run and run != "success" and verdict != "NICHT AUSGEFÜHRT" and not failed and not aborted:
        items.append("Der Testschritt endete mit `%s`, obwohl keine Testfehler gemeldet wurden (z. B. Sanitizer-Fund beim Beenden)." % run)
    if copies and copies != "success":
        items.append("Prüfung der Legacy-Kopien (`cmp`): `%s`." % copies)
    if not copies:
        items.append("Prüfung der Legacy-Kopien: nicht gelaufen.")
    w("\n".join("- " + i for i in items) if items else "Keine.")
    w("")
    if a.log and os.path.exists(a.log) and (aborted or verdict in ("ABGEBROCHEN", "FEHLGESCHLAGEN")):
        tail = open(a.log, encoding="utf-8", errors="replace").read().splitlines()[-25:]
        w("<details><summary>Ende der Konsolenausgabe</summary>\n\n```\n" + "\n".join(tail).replace("```", "'''") + "\n```\n</details>\n")

    w("## Firmware-Builds (getrennt von den Host-Tests)\n")
    w("Gebaut von `.github/workflows/next.yml` (ESP-IDF in Container). Der Status wird für denselben Commit abgefragt; ein Build ist kein Test.\n")
    fw = None
    if a.firmware and os.path.exists(a.firmware):
        try:
            fw = json.load(open(a.firmware, encoding="utf-8"))
        except ValueError:
            fw = None
    if not fw:
        w("**Nicht ermittelt:** Für diesen Commit liegt kein Firmware-Lauf vor (Workflow nicht ausgelöst, noch nicht gestartet oder Abfrage nicht möglich). Kein Build gilt als erfolgreich.\n")
    else:
        w("| Job | Status | Ergebnis |\n|---|---|---|")
        for j in fw:
            concl = j.get("conclusion") or "–"
            res = {"success": "erfolgreich", "failure": "**fehlgeschlagen**", "cancelled": "**abgebrochen**", "skipped": "**übersprungen**"}.get(concl, concl)
            if j.get("status") != "completed":
                res = "**noch nicht abgeschlossen**"
            w("| %s | %s | %s |" % (cell(j.get("name", "?")), cell(j.get("status", "?")), res))
        w("")

    w("## Hardwaretests\n")
    w("**Nicht ausgeführt.** Weder Gateway, Controller noch Diagnosegerät werden in der CI auf Hardware geflasht oder getestet; kein Punkt hier gilt als bestanden. "
      "Offene Hardwaretests laut `next/README.md` (Abschnitt „Offene Hardwaretests“, automatisch übernommen):\n")
    hw = hardware_list(a.next_readme)
    w("\n".join(hw) if hw else "_Abschnitt in `next/README.md` nicht gefunden._")
    w("")
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(o).rstrip() + "\n")
    print("Bericht: %s (%s)" % (a.out, verdict))
    return 0


if __name__ == "__main__":
    sys.exit(main())
