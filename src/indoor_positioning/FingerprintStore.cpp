#include "FingerprintStore.h"
#include <Preferences.h>

namespace ips {

namespace {
constexpr const char *kNamespace = "ips";
constexpr const char *kKey = "cal";
// Bump when Calibration's layout changes: old blobs are then ignored.
constexpr uint32_t kMagic = 0x49505331; // "IPS1"
constexpr uint16_t kVersion = 1;

struct Blob {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  Calibration cal;
};

Blob blob; // ~4 KB, kept off the stack.
} // namespace

bool loadCalibration(Calibration &out) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return false;
  const size_t len = prefs.getBytesLength(kKey);
  bool ok = len == sizeof(Blob) && prefs.getBytes(kKey, &blob, sizeof(Blob)) == sizeof(Blob);
  prefs.end();
  ok = ok && blob.magic == kMagic && blob.version == kVersion && blob.size == sizeof(Calibration) &&
       blob.cal.pointCount >= 2 && blob.cal.pointCount <= kMaxReferencePoints;
  for (uint8_t p = 0; ok && p < blob.cal.pointCount; ++p) ok = blob.cal.points[p].count <= kMaxProfileAps;
  if (ok) out = blob.cal;
  return ok;
}

bool saveCalibration(const Calibration &cal) {
  blob.magic = kMagic;
  blob.version = kVersion;
  blob.size = sizeof(Calibration);
  blob.cal = cal;
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  const bool ok = prefs.putBytes(kKey, &blob, sizeof(Blob)) == sizeof(Blob);
  prefs.end();
  return ok;
}

void eraseCalibration() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  prefs.remove(kKey);
  prefs.end();
}

} // namespace ips
