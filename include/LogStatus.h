#pragma once
#include <cstdint>

// What the SD logger reports to the UI; plain data, no Arduino dependency.
struct LogStatus {
  enum class State : uint8_t { NoCard, Idle, Recording, Error };
  State state = State::NoCard;
  unsigned session = 0;      // Current or last session.
  uint32_t fileBytes = 0;
  uint32_t samples = 0;
  uint32_t dropped = 0;      // Records lost because the logger's queue was full.
  uint32_t recordingMs = 0;
  uint32_t cardMb = 0;
  char message[40] = "";
};
