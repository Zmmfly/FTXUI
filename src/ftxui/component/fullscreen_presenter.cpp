// Fullscreen alternate-screen differential presenter for FTXUI.
#include "ftxui/component/fullscreen_presenter.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace ftxui::detail {
namespace {

constexpr std::string_view kBeginSynchronizedOutput{"\x1B[?2026h"};
constexpr std::string_view kEndSynchronizedOutput{"\x1B[?2026l"};
constexpr std::string_view kHideCursor{"\x1B[?25l"};

std::string CursorPosition(int x, int y, int width, int height) {
  const int bounded_x = std::clamp(x, 0, std::max(width - 1, 0));
  const int bounded_y = std::clamp(y, 0, std::max(height - 1, 0));
  return "\x1B[" + std::to_string(bounded_y + 1) + ";" +
         std::to_string(bounded_x + 1) + "H";
}

void AppendCursorState(std::string& output,
                       int cursor_x,
                       int cursor_y,
                       int width,
                       int height,
                       Screen::Cursor::Shape cursor_shape) {
  output += CursorPosition(cursor_x, cursor_y, width, height);
  if (cursor_shape == Screen::Cursor::Hidden) {
    output += "\x1B[?25l";
    return;
  }
  output += "\x1B[?25h";
  output += "\x1B[" + std::to_string(static_cast<int>(cursor_shape)) + " q";
}

}  // namespace

std::vector<std::string> SplitFullscreenRows(std::string_view frame) {
  std::vector<std::string> rows;
  std::size_t begin = 0;
  for (;;) {
    const auto end = frame.find("\r\n", begin);
    if (end == std::string_view::npos) {
      rows.emplace_back(frame.substr(begin));
      return rows;
    }
    rows.emplace_back(frame.substr(begin, end - begin));
    begin = end + 2;
  }
}

std::string PresentFullscreenRows(
    const std::vector<std::string>& previous_rows,
    const std::vector<std::string>& current_rows,
    int width,
    int height,
    int cursor_x,
    int cursor_y,
    Screen::Cursor::Shape cursor_shape,
    bool force_full) {
  std::string output;
  output += kBeginSynchronizedOutput;
  // Hiding the cursor also prevents visible cursor sweeps on terminals that
  // safely ignore the synchronized-output private mode.
  output += kHideCursor;

  const bool full =
      force_full || previous_rows.size() != current_rows.size();
  if (full) {
    output += "\x1B[2J\x1B[H";
    for (std::size_t row = 0; row < current_rows.size(); ++row) {
      if (row != 0) {
        output += "\r\n";
      }
      output += current_rows[row];
    }
  } else {
    for (std::size_t row = 0; row < current_rows.size(); ++row) {
      if (current_rows[row] == previous_rows[row]) {
        continue;
      }
      output += "\x1B[" + std::to_string(row + 1) + ";1H";
      output += current_rows[row];
    }
  }

  AppendCursorState(
      output, cursor_x, cursor_y, width, height, cursor_shape);
  output += kEndSynchronizedOutput;
  return output;
}

}  // namespace ftxui::detail
