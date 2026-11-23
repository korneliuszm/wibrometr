#pragma once
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "RecorderCore.h"
#include "Spectrum.h"

// Statistics and spectrum of the last recorded measurement, shown next to the
// chart. No hardware access; tested on a PC (test/vibration_checks.cpp).

struct AxisStats {
  float mean = 0;  // Includes gravity: the orientation of the sensor.
  float rms = 0;   // Of the deviation from the mean (the vibration itself).
  float peak = 0;  // Largest |deviation from the mean|.
  float p2p = 0;   // Max - min.
};

struct SpectrumPeak {
  float hz = 0, amp = 0;  // Interpolated frequency and amplitude (m/s2) of one line.
};

struct MeasStats {
  static constexpr std::size_t kFftSize = 1024;   // Window around the strongest moment.
  static constexpr std::size_t kSpectrumCols = 128;  // 0 .. Nyquist, kFftSize/2/kSpectrumCols bins each.

  bool valid = false;
  std::size_t samples = 0;
  float durationS = 0;
  float rateHz = 0;
  AxisStats axis[3];
  float magRms = 0;  // RMS of the vector length of the deviation.
  float magPeak = 0;
  bool hasSpectrum = false;  // False for recordings shorter than the FFT window.
  float domHz = 0, domAmp = 0;  // Strongest line above ~1 Hz, amplitude of the XYZ vector (m/s2).
  float nyquistHz = 0;
  float spectrum[kSpectrumCols]{};  // Max of the vector amplitude in each column.
  static constexpr std::size_t kPeaks = 5;
  float fft[kFftSize / 2 + 1]{};    // The same spectrum in full resolution: fft[k] is at k * binHz.
  float binHz = 0;
  int peakCount = 0;                // Local maxima above ~1 Hz, strongest first.
  SpectrumPeak peaks[kPeaks];
};

namespace measstats {

inline float comp(const Accel &a, int i) { return i == 0 ? a.x : i == 1 ? a.y : a.z; }

// s: n samples, rateHz: mean sample rate (0 = unknown, no spectrum). Fills `r` in
// place: MeasStats is ~2.7 KB, too much for a temporary on the Arduino loop stack.
inline void computeInto(MeasStats &r, const Accel *s, std::size_t n, float rateHz) {
  static_assert(std::is_trivially_copyable<MeasStats>::value, "cleared with memset");
  std::memset(static_cast<void *>(&r), 0, sizeof r);  // All defaults are zero.
  if (!s || n < 2) return;
  r.valid = true;
  r.samples = n;
  r.rateHz = rateHz;
  r.durationS = rateHz > 0 ? (n - 1) / rateHz : 0;

  double sum[3] = {0, 0, 0};
  float lo[3], hi[3];
  for (int a = 0; a < 3; ++a) lo[a] = hi[a] = comp(s[0], a);
  for (std::size_t i = 0; i < n; ++i)
    for (int a = 0; a < 3; ++a) {
      const float v = comp(s[i], a);
      sum[a] += v;
      lo[a] = v < lo[a] ? v : lo[a];
      hi[a] = v > hi[a] ? v : hi[a];
    }
  float mean[3];
  for (int a = 0; a < 3; ++a) mean[a] = float(sum[a] / n);

  double sq[3] = {0, 0, 0}, magSq = 0;
  float peak[3] = {0, 0, 0}, magPeak2 = 0;
  std::size_t strongest = 0;
  for (std::size_t i = 0; i < n; ++i) {
    float m2 = 0;
    for (int a = 0; a < 3; ++a) {
      const float d = comp(s[i], a) - mean[a];
      sq[a] += double(d) * d;
      peak[a] = fmaxf(peak[a], fabsf(d));
      m2 += d * d;
    }
    magSq += m2;
    if (m2 > magPeak2) { magPeak2 = m2; strongest = i; }
  }
  for (int a = 0; a < 3; ++a)
    r.axis[a] = {mean[a], float(std::sqrt(sq[a] / n)), peak[a], hi[a] - lo[a]};
  r.magRms = float(std::sqrt(magSq / n));
  r.magPeak = std::sqrt(magPeak2);

  constexpr std::size_t N = MeasStats::kFftSize;
  if (n < N || rateHz <= 0) return;
  std::size_t start = strongest > N / 2 ? strongest - N / 2 : 0;
  if (start + N > n) start = n - N;
  static SampleWindow<N> window;  // 16 KB, off the stack.
  static float amp[3][N / 2 + 1];
  window.clear();
  const float periodUs = 1e6f / rateHz;
  for (std::size_t i = 0; i < N; ++i) window.add(s[start + i], uint32_t(i * periodUs));
  for (int a = 0; a < 3; ++a) amplitudeSpectrum(window, a, amp[a]);

  constexpr std::size_t per = (N / 2) / MeasStats::kSpectrumCols;
  r.nyquistHz = rateHz / 2;
  float best = 0;
  std::size_t bestBin = 0;
  const std::size_t minBin = std::size_t(1.0f * N / rateHz) + 1;  // Skip DC leakage below ~1 Hz.
  for (std::size_t k = 0; k <= N / 2; ++k) {
    const float v = std::sqrt(amp[0][k] * amp[0][k] + amp[1][k] * amp[1][k] + amp[2][k] * amp[2][k]);
    if (k >= minBin && v > best) { best = v; bestBin = k; }
    r.fft[k] = v;
    const std::size_t col = k == N / 2 ? MeasStats::kSpectrumCols - 1 : k / per;
    r.spectrum[col] = fmaxf(r.spectrum[col], v);
  }
  r.hasSpectrum = true;
  r.binHz = rateHz / N;
  r.domHz = bestBin * r.binHz;
  r.domAmp = best;

  // Lines: local maxima at least 5 % of the strongest (Hann sidelobes are ~3 %), the
  // frequency and amplitude refined by a parabola through the three bins.
  for (std::size_t k = (minBin > 1 ? minBin : 1); k < N / 2; ++k) {
    const float a = r.fft[k - 1], b = r.fft[k], c = r.fft[k + 1];
    if (!(b > a && b >= c) || b < 0.05f * best || b < 0.01f) continue;
    const float denom = a - 2 * b + c;
    const float delta = denom < 0 ? 0.5f * (a - c) / denom : 0;
    const SpectrumPeak p{(k + delta) * r.binHz, b - 0.25f * (a - c) * delta};
    int at = r.peakCount;  // Insert, strongest first, keeping the top kPeaks.
    while (at > 0 && r.peaks[at - 1].amp < p.amp) --at;
    if (at >= int(MeasStats::kPeaks)) continue;
    const int last = r.peakCount < int(MeasStats::kPeaks) ? r.peakCount : int(MeasStats::kPeaks) - 1;
    for (int i = last; i > at; --i) r.peaks[i] = r.peaks[i - 1];
    r.peaks[at] = p;
    if (r.peakCount < int(MeasStats::kPeaks)) ++r.peakCount;
  }
  // Refine the strongest line only when it is a local maximum: a line below ~1.5 Hz
  // (body bounce) leaves its strongest bin on a falling slope, and peaks[0] would
  // then be a weaker line elsewhere.
  if (r.peakCount > 0 && std::fabs(r.peaks[0].hz - r.domHz) <= r.binHz) {
    r.domHz = r.peaks[0].hz;
    r.domAmp = r.peaks[0].amp;
  }
}

inline MeasStats compute(const Accel *s, std::size_t n, float rateHz) {
  MeasStats r;
  computeInto(r, s, n, rateHz);
  return r;
}

}  // namespace measstats

// Copy of the samples of the measurement being written, for the statistics. The
// sensor thread fills it between begin() and end(); the UI thread reads it
// between tryAcquire() and release(). If the UI still holds it when a new
// measurement opens, that measurement is simply not captured (it is still
// written to the card). The two flags are checked in opposite order on the two
// sides (seq_cst), so they can never both succeed.
class MeasCapture {
public:
  static constexpr std::size_t kCapacity = 16384;  // 30 s + 1 s pre-roll at 512 Hz.

  struct View {
    const Accel *data = nullptr;
    std::size_t count = 0;
    float rateHz = 0;
    uint32_t seq = 0;
  };

  // Storage for kCapacity samples (PSRAM); without it nothing is captured.
  void attach(Accel *storage) { buf_ = storage; }

  // Sensor thread.
  void begin() {
    count_ = 0;
    firstUs_ = lastUs_ = 0;
    if (!buf_) return;
    active_.store(true);
    if (reading_.load()) active_.store(false);
  }
  void add(int32_t relUs, const Accel &a) {
    if (!active_.load(std::memory_order_relaxed)) return;
    if (count_ == 0) firstUs_ = relUs;
    lastUs_ = relUs;
    if (count_ < kCapacity) buf_[count_++] = a;
  }
  void end() {
    if (!active_.load()) return;
    done_ = count_;
    doneSpanUs_ = int64_t(lastUs_) - firstUs_;
    seq_.fetch_add(1);
    active_.store(false);
  }

  // UI thread: a finished capture newer than `seen`, or false.
  bool tryAcquire(uint32_t seen, View &out) {
    if (seq_.load() == seen || active_.load()) return false;
    reading_.store(true);
    if (active_.load()) {
      reading_.store(false);
      return false;
    }
    out.data = buf_;
    out.count = done_;
    out.seq = seq_.load();
    out.rateHz = doneSpanUs_ > 0 && done_ > 1 ? (done_ - 1) * 1e6f / float(doneSpanUs_) : 0;
    return true;
  }
  void release() { reading_.store(false); }

private:
  Accel *buf_ = nullptr;
  std::size_t count_ = 0, done_ = 0;
  int32_t firstUs_ = 0, lastUs_ = 0;
  int64_t doneSpanUs_ = 0;
  std::atomic<bool> active_{false}, reading_{false};
  std::atomic<uint32_t> seq_{0};
};

// Passes a measurement through to `inner` (the card writer) and copies its samples
// into `capture`.
class CapturingSink : public MeasSink {
public:
  CapturingSink(MeasSink &inner, MeasCapture &capture) : inner_(inner), capture_(capture) {}
  bool open(int thresholdDeci) override {
    if (!inner_.open(thresholdDeci)) return false;
    capture_.begin();
    return true;
  }
  void write(int32_t relUs, const Accel &a) override {
    capture_.add(relUs, a);
    inner_.write(relUs, a);
  }
  void close() override {
    capture_.end();
    inner_.close();
  }
  void missed(uint32_t count) override { inner_.missed(count); }

private:
  MeasSink &inner_;
  MeasCapture &capture_;
};
