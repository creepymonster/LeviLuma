#include "ble_scheduler.h"

void BleScheduler::onConnected(const LightSnapshot &s) {
  lastPower_ = -1;
  // Modus unbekannt: Musik-aus wird beim Verbinden erzwungen (nur Familien mit Musikmodus)
  lastMusic_ = drv_.caps().music;
  lastBright_ = -1;
  lastColorRev_ = s.colorRev - 1;
  instantColor_ = true;
  shown_ = ColorMath::fromRgb(s.r, s.g, s.b);
  fadeLeft_ = 0;
  eqPending_ = false;
}

bool BleScheduler::next(const LightSnapshot &s, const LeviCal::Model &cal, uint32_t nowMs, Step &out) {
  out = Step();
  const BandCaps caps = drv_.caps();
  const bool musicOn = caps.music && s.music; // ohne Musikmodus der Familie bleibt das Band im Farbmodus

  if (caps.music && s.music != lastMusic_) {
    out.cmd = Cmd::Music;
    out.len = drv_.music(s.music, out.packet);
    return true;
  }
  if (caps.micEq && eqPending_) { // Mikrofon-EQ direkt nach 'Mikrofon an', wie die App
    out.cmd = Cmd::MicEq;
    out.len = drv_.micEq(cfg_.micEqMode, out.packet);
    return true;
  }
  const bool brightPending = s.brightness != lastBright_;

  if (caps.nativeBrightness && brightPending) {
    out.cmd = Cmd::Brightness;
    out.len = drv_.brightness(bandBrightness(s.brightness), out.packet);
    return true;
  }
  if (s.power >= 0 && s.power != lastPower_) {
    out.cmd = Cmd::Power;
    out.len = drv_.power(s.power == 1, out.packet);
    return true;
  }
  // Wie Legacy: (!s.raw || millis() - lastRawSend >= 400), lastRawSend startet bei 0.
  bool rawReady = !s.raw || (uint32_t)(nowMs - lastRawSend_) >= cfg_.rawMinGapMs;
  // Ohne eigenes Helligkeitspaket steckt die Helligkeit in der Farbe: dann auch bei Helligkeitsaenderung senden
  bool colorPending = s.colorRev != lastColorRev_ || fadeLeft_ > 0 || (!caps.nativeBrightness && brightPending);

  if (s.power == 1 && !musicOn && colorPending && rawReady) {
    if (s.colorRev != lastColorRev_) { // neue Farbe: Ueberblendung ab dem zuletzt gesendeten Zwischenwert
      lastColorRev_ = s.colorRev;
      fadeRaw_ = s.raw;
      fadeTo_ = ColorMath::fromRgb(s.r, s.g, s.b);
      fadeFrom_ = shown_;
      fadeN_ = (s.raw || instantColor_) ? 1 : (cfg_.fadeSteps < 1 ? 1 : cfg_.fadeSteps);
      fadeLeft_ = fadeN_;
      instantColor_ = false;
    }
    uint8_t cc[3] = {s.r, s.g, s.b};
    int step = fadeN_;

    if (fadeLeft_ > 0) {
      step = fadeN_ - fadeLeft_ + 1;
      if (!fadeRaw_) {
        shown_ = ColorMath::mix(fadeFrom_, fadeTo_, (float)step / fadeN_);
      }
    }
    // Nur Helligkeit geaendert (fadeLeft_ == 0): zuletzt gezeigte Farbe erneut, jetzt mit neuer Helligkeit
    if (!fadeRaw_) {
      ColorMath::toRgb(shown_, cc);
      LeviCal::apply(cal, cc[0], cc[1], cc[2], cc);
    }
    out.cmd = Cmd::Color;
    out.fadeStep = step;
    out.fadeN = fadeN_;
    out.raw = fadeRaw_;
    out.r = cc[0];
    out.g = cc[1];
    out.b = cc[2];
    out.len = drv_.color(cc[0], cc[1], cc[2], s.brightness, out.packet);
    return true;
  }
  return false;
}

void BleScheduler::commit(const Step &st, bool ok, const LightSnapshot &s, uint32_t nowMs) {
  if (!ok) {
    return;
  }
  switch (st.cmd) {
  case Cmd::Music:
    lastMusic_ = s.music;
    eqPending_ = s.music;
    break;
  case Cmd::MicEq:
    eqPending_ = false;
    break;
  case Cmd::Brightness:
    lastBright_ = s.brightness;
    break;
  case Cmd::Power:
    lastPower_ = s.power;
    break;
  case Cmd::Color:
    if (fadeLeft_ > 0) {
      fadeLeft_--;
    }
    if (!drv_.caps().nativeBrightness) {
      lastBright_ = s.brightness; // Helligkeit ging mit dem Farbpaket raus
    }
    if (s.raw) {
      lastRawSend_ = nowMs;
    }
    break;
  default:
    break;
  }
}
