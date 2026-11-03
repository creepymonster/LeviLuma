#include "calib_store.h"
#include <string.h>

void CalibStore::defaultPts(LeviCal::Model &m) const {
  LeviCal::defaultPts(m, def_.gr, def_.gg, def_.gb);

  if (def_.hasPoints) {
    memcpy(m.pts, def_.points, sizeof(m.pts));
  }
}

void CalibStore::points(uint8_t out[LeviCal::N * 3]) const {
  memcpy(out, model_.pts, sizeof(model_.pts));
}

// Revision persistent mitzaehlen, damit Controller Aenderungen auch ueber Neustarts erkennen.
void CalibStore::bumpRevision() {
  revision_ = (uint16_t)(revision_ + 1);
  if (revision_ == 0) {
    revision_ = 1;
  }
  kv_.setBlob(KEY_REV, &revision_, sizeof(revision_));
}

void CalibStore::load() {
  LeviCal::Model m;
  defaultPts(m);
  bool custom = false;
  size_t len = 0;

  if (kv_.blobSize(KEY_POINTS, &len) && len == sizeof(m.pts)) {
    uint8_t buf[LeviCal::N * 3];
    size_t n = sizeof(buf);

    if (kv_.getBlob(KEY_POINTS, buf, &n) && n == sizeof(buf)) {
      memcpy(m.pts, buf, sizeof(buf));
      custom = true;
    }
  }
  uint16_t rev = 0;
  size_t rl = sizeof(rev);

  if (!kv_.getBlob(KEY_REV, &rev, &rl) || rl != sizeof(rev)) {
    rev = 1;
  }
  LeviCal::solve(m);
  model_ = m;
  custom_ = custom;
  revision_ = rev;
}

bool CalibStore::save(const uint8_t pts[LeviCal::N * 3]) {
  LeviCal::Model m = model_;
  memcpy(m.pts, pts, sizeof(m.pts));
  LeviCal::solve(m);

  if (!kv_.setBlob(KEY_POINTS, m.pts, sizeof(m.pts))) {
    return false;
  }
  model_ = m;
  custom_ = true;
  bumpRevision();
  return true;
}

bool CalibStore::reset() {
  if (!kv_.erase(KEY_POINTS)) {
    return false;
  }
  LeviCal::Model m;
  defaultPts(m);
  LeviCal::solve(m);
  model_ = m;
  custom_ = false;
  bumpRevision();
  return true;
}
