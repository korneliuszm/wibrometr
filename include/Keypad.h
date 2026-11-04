#pragma once
#include <cstddef>
#include <cstdint>
#include "TouchGestures.h"

// Numeric keypad for typing the hanger tag on the touch screen: layout, hit
// testing and the digits typed so far. No drawing, no hardware; tested on a PC.
enum class Key : int8_t { None = -1, D0 = 0, D1, D2, D3, D4, D5, D6, D7, D8, D9, Backspace = 10, Ok = 11 };

struct KeypadLayout {
  static constexpr int kCols = 3, kRows = 4;
  static constexpr int kX = 10, kY = 58, kKeyW = 96, kKeyH = 30, kPitchX = 100, kPitchY = 34;
  // Rows: 1 2 3 / 4 5 6 / 7 8 9 / Backspace 0 Ok.
  static constexpr Key keyAt(int col, int row) {
    return row < 3 ? Key(row * 3 + col + 1) : col == 0 ? Key::Backspace : col == 1 ? Key::D0 : Key::Ok;
  }
  static constexpr Rect rect(int col, int row) {
    return {int16_t(kX + col * kPitchX), int16_t(kY + row * kPitchY), kKeyW, kKeyH};
  }
  static constexpr Key hit(int x, int y) {
    for (int row = 0; row < kRows; ++row)
      for (int col = 0; col < kCols; ++col)
        if (rect(col, row).contains(x, y)) return keyAt(col, row);
    return Key::None;
  }
};

// The digits being typed; at most kMax, shown and saved as typed.
class TagEntry {
public:
  static constexpr std::size_t kMax = 8;
  constexpr void clear() { len_ = 0; digits_[0] = '\0'; }
  constexpr void set(const char *digits) {
    clear();
    for (const char *p = digits; p && *p && len_ < kMax; ++p)
      if (*p >= '0' && *p <= '9') digits_[len_++] = *p;
    digits_[len_] = '\0';
  }
  // Returns true when the entry is finished (Ok).
  constexpr bool press(Key k) {
    if (k == Key::Ok) return true;
    if (k == Key::Backspace) {
      if (len_) digits_[--len_] = '\0';
    } else if (k >= Key::D0 && k <= Key::D9 && len_ < kMax) {
      digits_[len_++] = char('0' + int(k));
      digits_[len_] = '\0';
    }
    return false;
  }
  constexpr const char *digits() const { return digits_; }
  constexpr std::size_t length() const { return len_; }

private:
  char digits_[kMax + 1] = {};
  std::size_t len_ = 0;
};
