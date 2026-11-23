#pragma once
#include <cstdint>

// Settings of the suspension vibration recorder (env waveshare_vibration).
//
// Sample rate limits of this board (QMI8658A datasheet Rev D, measured here):
//  - accelerometer-only ODRs are 1000 Hz and below (2000+ exist only with the gyro
//    on, as 1793.6 / 3587.2 / 7174.4 Hz, 12 bytes per sample);
//  - the FIFO drops every sample made while it is being read (8.7), and a read over
//    400 kHz I2C takes ~135 us per sample: FIFO reading lost ~35-40 % at 1-2 kHz;
//  - data-ready is only on INT2, which is not wired: the data registers are polled
//    every 1 ms (RTOS tick), which keeps 500 Hz with a few lost samples, counted.
// Accelerometer-only ODR comes from the chip's internal oscillator (measured
// ~512 Hz for "500"), so the recorder measures the real rate for the file header.
// Bandwidth (datasheet table 11, 500 Hz): 137.5 Hz with the low-pass off (the
// built-in 27.5 % ODR anti-alias filter), 66.9 Hz with LPF mode 11.
namespace vib {

constexpr float kSampleRateHz = 500;      // QMI8658 accelerometer ODR.
constexpr uint8_t kRange = 2;             // 0..3 = +-2/4/8/16 g; 2 = +-8 g.
constexpr bool kLowPass = false;          // Off: 137.5 Hz bandwidth for bearing/roller impacts.
constexpr uint8_t kLowPassMode = 3;       // If on: 13.37 % of the ODR = 67 Hz at 500 Hz.
constexpr bool kUseFifo = false;          // FIFO path: loses samples, see above.
constexpr uint32_t kFifoReadMs = 5;
static_assert(kUseFifo || kSampleRateHz <= 500, "polling at 1 kHz needs 500 Hz or less");

constexpr uint32_t kPreRollUs = 1000000;  // Pre-trigger part of each measurement.
constexpr uint32_t kQuietUs = 3000000;    // This long below the threshold ends a measurement.
constexpr uint32_t kWarmupUs = 1000000;   // No trigger after start or a data gap (filter settles).
constexpr uint32_t kMaxRecordUs = 30000000;  // A measurement ends 30 s after its trigger at the latest.

constexpr uint32_t kFrameMs = 50;         // Screen refresh: 20 frames per second.
constexpr int kBins = 136;                // Chart columns...
constexpr uint32_t kBinMs = 40;           // ...of 40 ms each: a 5.44 s window.

constexpr int kMaxPeaks = 512;            // Peaks of one session kept for the second screen.
constexpr int kPages = 2;                 // Screens, switched by a horizontal swipe.

} // namespace vib
