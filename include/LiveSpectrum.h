#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "MeasStats.h"
#include "Spectrum.h"

// Spectrum of the live signal for the first screen: the last kSize samples of the dynamic
// acceleration (~2 s), per axis, reduced to kCols columns for a small panel, and the strongest
// line of the XYZ vector. Same method as the measurement spectrum (MeasStats: Hann window, mean
// removed, peaks refined by a parabola). The UI loop feeds every sample and recomputes a few
// times a second. Not reentrant (shares amplitudeSpectrum's scratch buffers with MeasStats,
// which also runs on the UI loop). No hardware; tested on a PC.
class LiveSpectrum {
public:
  static constexpr std::size_t kSize = MeasStats::kFftSize;
  static constexpr std::size_t kBins = kSize / 2 + 1;
  static constexpr int kCols = 96;

  void add(const Accel &dynamic, uint32_t us) { window_.add(dynamic, us); }
  // A gap in the samples (sensor lost, USB mode): the window starts over.
  void clear() {
    window_.clear();
    valid_ = false;
  }
  bool ready() const { return window_.full(); }
  bool valid() const { return valid_; }

  // Recomputes from the current window; does nothing until it is full or while the rate is unknown.
  void compute() {
    if (!window_.full()) return;
    const float rate = window_.rateHz();
    if (rate <= 0) return;
    for (int a = 0; a < 3; ++a) amplitudeSpectrum(window_, a, amp_[a]);
    for (auto &axis : col_)
      for (float &v : axis) v = 0;
    float best = 0;
    for (std::size_t k = 0; k < kBins; ++k) {
      vec_[k] = std::sqrt(amp_[0][k] * amp_[0][k] + amp_[1][k] * amp_[1][k] + amp_[2][k] * amp_[2][k]);
      if (k < 2) continue;  // Leakage of the removed mean.
      const int c = int(k * kCols / kBins);
      for (int a = 0; a < 3; ++a) col_[a][c] = fmaxf(col_[a][c], amp_[a][k]);
      best = fmaxf(best, vec_[k]);
    }
    rateHz_ = rate;
    nyquistHz_ = rate / 2;
    top_ = fmaxf(best, 0.05f);  // Do not blow up the noise floor of a quiet signal.
    const float binHz = rate / kSize;
    const std::size_t minBin = std::size_t(1.0f * kSize / rate) + 1;  // Above ~1 Hz.
    SpectrumPeak p;
    if (measstats::findPeaks(vec_, kBins, minBin, binHz, best, &p, 1) == 1) {
      domHz_ = p.hz;
      domAmp_ = p.amp;
    } else {
      domHz_ = domAmp_ = 0;
    }
    valid_ = true;
  }

  float rateHz() const { return rateHz_; }
  float nyquistHz() const { return nyquistHz_; }
  float domHz() const { return domHz_; }     // 0: no clear line.
  float domAmp() const { return domAmp_; }
  float top() const { return top_; }         // Largest column value (for the scale), at least 0.05.
  float column(int axis, int c) const { return col_[axis][c]; }

private:
  SampleWindow<kSize> window_;
  float amp_[3][kBins]{};
  float vec_[kBins]{};
  float col_[3][kCols]{};
  float rateHz_ = 0, nyquistHz_ = 0, domHz_ = 0, domAmp_ = 0, top_ = 0.05f;
  bool valid_ = false;
};
