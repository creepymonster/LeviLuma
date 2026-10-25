#!/usr/bin/env python3
# Screenshots der Web-UI (include/WebUi.h) in Desktop- und Mobilbreite.
# Die Seite laeuft im Browser (Playwright, Chromium). Die API ist gemockt; die Farben kommen aus Palette.h.
# Aufruf: webui.py <repo-root> <ausgabe-wurzel>   ->  <ausgabe-wurzel>/web/*.png
import json, os, re, sys
from playwright.sync_api import sync_playwright

repo, out_root = sys.argv[1], sys.argv[2]
out = os.path.join(out_root, "web")
os.makedirs(out, exist_ok=True)

header = open(os.path.join(repo, "include/WebUi.h"), encoding="utf-8").read().replace("@@NAME@@", "LeviLuma")
page_html = re.search(r'R"LEVIHTML\((.*?)\)LEVIHTML"', header, re.S).group(1)

palette_h = open(os.path.join(repo, "include/Palette.h"), encoding="utf-8").read()
entries = re.findall(r'LEVI_PICKER_ENTRY\("([^"]+)",\s*(\d+),\s*(\d+),\s*(\d+)\)', palette_h)
colors = [{"name": n, "hex": "#%02x%02x%02x" % (int(r), int(g), int(b))} for n, r, g, b in entries]

# Zustand: eingeschaltet, verbunden, Farbe "Rot" aus dem Farbset
state = {"wifi": True, "ble": True, "power": "on", "r": 255, "g": 64, "b": 64,
         "brightness": 75, "music": False, "revision": 1, "sentRevision": 1}
BASE = "http://leviluma.test"

def api(route):
    path = route.request.url[len(BASE):]
    if path.startswith("/api/state"):
        body = json.dumps(state)
    elif path.startswith("/api/palette"):
        body = json.dumps({"colors": colors, "music": True})
    else:
        body = "{}"
    route.fulfill(status=200, content_type="application/json", body=body,
                  headers={"Cache-Control": "no-store"})

def index(route):
    route.fulfill(status=200, content_type="text/html; charset=utf-8", body=page_html)

VIEWS = [
    ("desktop_1280x800", dict(width=1280, height=800), False),
    ("mobile_390x844", dict(width=390, height=844), True),
]

with sync_playwright() as p:
    browser = p.chromium.launch()
    for name, viewport, mobile in VIEWS:
        ctx = browser.new_context(viewport=viewport, is_mobile=mobile, has_touch=mobile, device_scale_factor=1)
        page = ctx.new_page()
        errors = []
        page.on("pageerror", lambda e: errors.append(str(e)))
        page.route(BASE + "/", index)
        page.route(BASE + "/api/**", api)
        page.goto(BASE + "/")
        page.wait_for_timeout(800)
        page.screenshot(path=os.path.join(out, name + ".png"), full_page=True)
        print("geschrieben:", name + ".png", "JS-Fehler:", len(errors))
        for e in errors:
            print("  ", e)
        ctx.close()
    browser.close()
