#include "BleRanging.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ble {

namespace {
// Adverts are stamped in the BLE task and may be a few ms newer than the
// caller's "now"; treat those as age 0 instead of a huge unsigned age.
uint32_t age(uint32_t nowMs, uint32_t thenMs) {
  return int32_t(nowMs - thenMs) < 0 ? 0 : nowMs - thenMs;
}
} // namespace

bool Address::operator==(const Address &o) const { return std::memcmp(b, o.b, 6) == 0; }

bool Address::empty() const {
  for (uint8_t v : b)
    if (v) return false;
  return true;
}

void Address::format(char *out, std::size_t len) const {
  std::snprintf(out, len, "%02X:%02X:%02X:%02X:%02X:%02X", b[0], b[1], b[2], b[3], b[4], b[5]);
}

bool Address::parse(const char *text, Address &out) {
  unsigned v[6];
  char tail;
  if (!text || std::sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x%c", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &tail) != 6)
    return false;
  for (int i = 0; i < 6; ++i) out.b[i] = uint8_t(v[i]);
  return true;
}

float distanceM(float rssiDbm, float rssiAt1mDbm, float exponent) {
  if (!(exponent > 0) || !std::isfinite(rssiDbm)) return NAN;
  return std::pow(10.0f, (rssiAt1mDbm - rssiDbm) / (10.0f * exponent));
}

void RssiFilter::add(int8_t rssi, uint32_t nowMs) {
  rssi_[head_] = rssi;
  ms_[head_] = nowMs;
  head_ = (head_ + 1) % kRssiSamples;
  if (count_ < kRssiSamples) ++count_;
}

std::size_t RssiFilter::recent(uint32_t nowMs) const {
  std::size_t n = 0;
  for (std::size_t i = 0; i < count_; ++i)
    if (age(nowMs, ms_[i]) <= kRssiWindowMs) ++n;
  return n;
}

float RssiFilter::median(uint32_t nowMs) const {
  int8_t v[kRssiSamples];
  std::size_t n = 0;
  for (std::size_t i = 0; i < count_; ++i)
    if (age(nowMs, ms_[i]) <= kRssiWindowMs) v[n++] = rssi_[i];
  if (!n) return -128;
  std::sort(v, v + n);
  return n % 2 ? v[n / 2] : 0.5f * (v[n / 2 - 1] + v[n / 2]);
}

void Tracker::setTarget(const Address &addr) {
  target_ = addr;
  filter_.clear();
  seenMs_ = 0;
  intervalMs_ = 0;
  targetApple_ = false;
}

void Tracker::add(const Advert &a) {
  if (!target_.empty() && a.addr == target_) {
    if (seenMs_) {
      // Gaps over 2 s are scanner pauses or losses, not the advert period.
      const uint32_t gap = a.ms - seenMs_;
      if (gap > 0 && gap < 2000) intervalMs_ = intervalMs_ ? 0.8f * intervalMs_ + 0.2f * gap : float(gap);
    }
    filter_.add(a.rssi, a.ms);
    seenMs_ = a.ms;
    lastRssi_ = a.rssi;
    targetApple_ = a.apple;
    return;
  }
  // Other devices: smoothed RSSI per address, oldest slot reused when full.
  Candidate *c = nullptr;
  for (std::size_t i = 0; i < candCount_ && !c; ++i)
    if (cand_[i].addr == a.addr) c = &cand_[i];
  if (!c) {
    if (candCount_ < kMaxCandidates) {
      c = &cand_[candCount_++];
    } else {
      c = &cand_[0];
      for (std::size_t i = 1; i < candCount_; ++i)
        if (int32_t(cand_[i].lastMs - c->lastMs) < 0) c = &cand_[i];
    }
    *c = Candidate{};
    c->addr = a.addr;
    c->rssi = a.rssi;
  } else {
    c->rssi = 0.7f * c->rssi + 0.3f * a.rssi;
  }
  c->apple = a.apple;
  c->lastMs = a.ms;
}

float Tracker::distance(uint32_t nowMs) const {
  const float rssi = filter_.median(nowMs);
  return rssi > -128 ? distanceM(rssi, rssiAt1m_) : NAN;
}

bool Tracker::fresh(uint32_t nowMs) const { return seenMs_ && age(nowMs, seenMs_) < kFreshMs; }

uint32_t Tracker::ageMs(uint32_t nowMs) const { return age(nowMs, seenMs_); }

bool Tracker::strongest(uint32_t nowMs, Candidate &out) const {
  bool found = false;
  for (std::size_t i = 0; i < candCount_; ++i) {
    const Candidate &c = cand_[i];
    if (age(nowMs, c.lastMs) > kFreshMs) continue;
    if (!found || c.rssi > out.rssi) {
      out = c;
      found = true;
    }
  }
  return found;
}

std::size_t Tracker::candidateCount(uint32_t nowMs) const {
  std::size_t n = 0;
  for (std::size_t i = 0; i < candCount_; ++i)
    if (age(nowMs, cand_[i].lastMs) <= kFreshMs) ++n;
  return n;
}

} // namespace ble
