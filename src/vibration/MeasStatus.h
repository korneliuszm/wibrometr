#pragma once
#include <cstdint>

// What the measurement writer reports to the UI; plain data.
struct MeasStatus {
  enum class State : uint8_t { NoCard, Idle, Recording, Error, Parked };
  State state = State::NoCard;
  char message[32] = "";      // Polish, no diacritics: "brak karty", "gotowa", ...
  unsigned number = 0;        // Current or last measurement (M<number>.CSV).
  uint32_t samples = 0;       // Samples written to the current/last file.
  uint32_t fileBytes = 0;
  uint32_t recordingMs = 0;   // Time since the file was opened (Recording only).
  uint32_t cardMb = 0;        // Card size.
  uint32_t dropped = 0;       // Records lost since boot because the queue was full.
  uint32_t measurements = 0;  // Mnnnn.CSV files in /MEAS.
  uint32_t recoveries = 0;    // Card glitches survived by remounting (since boot).
};
