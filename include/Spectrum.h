#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "SignalHistory.h"

// Last N raw samples with arrival times, oldest first via at().
template <std::size_t N> class SampleWindow {
  static_assert(N >= 2 && (N & (N - 1)) == 0, "FFT needs a power of two");
  Accel samples_[N]{};
  uint32_t ms_[N]{};
  std::size_t head_ = 0, count_ = 0;
public:
  constexpr void add(Accel v, uint32_t ms) {
    samples_[head_] = v;
    ms_[head_] = ms;
    head_ = (head_ + 1) % N;
    if (count_ < N) ++count_;
  }
  constexpr void clear() { head_ = count_ = 0; }
  constexpr std::size_t size() const { return count_; }
  constexpr bool full() const { return count_ == N; }
  // Caller supplies an index in [0, size()).
  constexpr Accel at(std::size_t index) const {
    return samples_[(head_ + N - count_ + index) % N];
  }
  constexpr uint32_t newestMs() const { return ms_[(head_ + N - 1) % N]; }
  // Mean sample rate over the window; 0 when it cannot be measured.
  constexpr float rateHz() const {
    if (count_ < 2) return 0;
    const uint32_t span = newestMs() - ms_[(head_ + N - count_) % N];
    return span ? (count_ - 1) * 1000.0f / span : 0;
  }
};

// Single-sided amplitude spectrum of one axis of a full window, in input units
// (peak amplitude of a sine). Mean is removed and a Hann window applied.
// out[k] lies at k * rate / N Hz. Not reentrant: uses static scratch buffers.
template <std::size_t N>
void amplitudeSpectrum(const SampleWindow<N> &window, int channel,
                       float (&out)[N / 2 + 1]) {
  static float re[N], im[N];
  constexpr float pi = 3.14159265358979f;
  float mean = 0;
  for (std::size_t i = 0; i < N; ++i) {
    Accel a = window.at(i);
    re[i] = channel == 0 ? a.x : channel == 1 ? a.y : a.z;
    mean += re[i];
  }
  mean /= N;
  for (std::size_t i = 0; i < N; ++i) {
    re[i] = (re[i] - mean) * (0.5f - 0.5f * cosf(2 * pi * i / N));
    im[i] = 0;
  }
  for (std::size_t i = 1, j = 0; i < N; ++i) { // Bit-reversal permutation.
    std::size_t bit = N >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j |= bit;
    if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; }
  }
  for (std::size_t len = 2; len <= N; len <<= 1) {
    const float step = -2 * pi / len;
    for (std::size_t j = 0; j < len / 2; ++j) {
      const float wr = cosf(step * j), wi = sinf(step * j);
      for (std::size_t i = j; i < N; i += len) {
        const std::size_t k = i + len / 2;
        const float tr = re[k] * wr - im[k] * wi, ti = re[k] * wi + im[k] * wr;
        re[k] = re[i] - tr; im[k] = im[i] - ti;
        re[i] += tr; im[i] += ti;
      }
    }
  }
  // Hann coherent gain is 1/2, so a full-scale bin sums to N/4 per side.
  for (std::size_t k = 0; k <= N / 2; ++k) {
    const float scale = (k == 0 || k == N / 2) ? 2.0f / N : 4.0f / N;
    out[k] = sqrtf(re[k] * re[k] + im[k] * im[k]) * scale;
  }
}
