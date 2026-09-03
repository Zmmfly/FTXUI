// Copyright 2026 The FTXUI Authors. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#ifndef FTXUI_COMPONENT_DETAIL_BACKSPACE_ACCELERATOR_HPP
#define FTXUI_COMPONENT_DETAIL_BACKSPACE_ACCELERATOR_HPP

#include <algorithm>  // for min
#include <chrono>     // for duration, milliseconds, steady_clock
#include <cmath>      // for floor
#include <cstddef>    // for size_t

#include "ftxui/component/event.hpp"  // for Event

namespace ftxui::detail {

// Internal refresh/resize notifications must not look like a physical key
// release. Ctrl+Space is canonicalized to Event::CtrlSpace by the terminal
// parser, so Event::Custom remains safe to recognize as internal-only here.
inline bool ResetsBackspaceAcceleration(const Event& event) {
  return event != Event::Backspace && event != Event::Custom;
}

// Smoothly add bounded glyph deletions to a terminal's native Backspace key
// repeat. Terminals generally do not report key-up events, so a quiet interval
// is treated as release; the next Backspace then starts at one glyph again.
class BackspaceAccelerator {
 public:
  using Clock = std::chrono::steady_clock;

  size_t OnBackspace(Clock::time_point now) {
    const bool clock_moved_backwards = active_ && now < last_event_;
    const auto reset_window = repeating_ ? kRepeatResetWindow
                                         : kInitialRepeatWindow;
    const bool repeat_expired =
        active_ && !clock_moved_backwards && now - last_event_ > reset_window;
    if (!active_ || clock_moved_backwards || repeat_expired) {
      Begin(now);
      return 1;
    }

    if (!repeating_) {
      repeating_ = true;
      repeat_started_ = now;
      last_event_ = now;
      extra_glyph_credit_ = 0.0;
      return 1;
    }

    const auto step = now - last_event_;
    const auto elapsed = now - repeat_started_;
    last_event_ = now;

    const double progress = std::min(
        1.0,
        std::chrono::duration<double>(elapsed).count() /
            std::chrono::duration<double>(kRampDuration).count());
    const double smooth_progress =
        progress * progress * (3.0 - 2.0 * progress);
    const double extra_glyphs_per_second =
        kMaximumExtraGlyphsPerSecond * smooth_progress;
    extra_glyph_credit_ +=
        extra_glyphs_per_second * std::chrono::duration<double>(step).count();

    const auto earned = static_cast<size_t>(std::floor(extra_glyph_credit_));
    extra_glyph_credit_ -= static_cast<double>(earned);
    // Slow terminal repeat cadences stay readable instead of jumping in
    // chunks, even when host scheduling delivers one late repeat event.
    return std::min<size_t>(1 + earned, kMaximumGlyphsPerEvent);
  }

  void Reset() {
    active_ = false;
    repeating_ = false;
    extra_glyph_credit_ = 0.0;
  }

 private:
  void Begin(Clock::time_point now) {
    active_ = true;
    repeating_ = false;
    last_event_ = now;
    repeat_started_ = now;
    extra_glyph_credit_ = 0.0;
  }

  // Allow the terminal's initial key-repeat delay, then infer release from a
  // pause longer than ordinary repeat cadence. The acceleration itself ramps
  // over 1.2 seconds and adds at most 24 glyphs/s to the terminal's native
  // repeat rate while bounding each individual visual update.
  static constexpr auto kInitialRepeatWindow = std::chrono::milliseconds(1000);
  static constexpr auto kRepeatResetWindow = std::chrono::milliseconds(160);
  static constexpr auto kRampDuration = std::chrono::milliseconds(1200);
  static constexpr double kMaximumExtraGlyphsPerSecond = 24.0;
  static constexpr size_t kMaximumGlyphsPerEvent = 4;

  bool active_ = false;
  bool repeating_ = false;
  Clock::time_point last_event_{};
  Clock::time_point repeat_started_{};
  double extra_glyph_credit_ = 0.0;
};

}  // namespace ftxui::detail

#endif  // FTXUI_COMPONENT_DETAIL_BACKSPACE_ACCELERATOR_HPP
