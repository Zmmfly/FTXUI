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
constexpr std::string_view kDisableLineWrap{"\x1B[?7l"};
constexpr std::string_view kResetStyleAndEraseLineTail{"\x1B[0m\x1B[K"};
constexpr std::string_view kResetStyleAndBlankLeftColumn{"\x1B[0m "};

std::string CursorPosition(int x, int y, int width, int height, int column_offset) {
  const int bounded_x = std::clamp(x, 0, std::max(width - 1, 0));
  const int bounded_y = std::clamp(y, 0, std::max(height - 1, 0));
  return "\x1B[" + std::to_string(bounded_y + 1) + ";" +
         std::to_string(bounded_x + 1 + column_offset) + "H";
}

void AppendCursorState(std::string& output,
                       int cursor_x,
                       int cursor_y,
                       int width,
                       int height,
                       Screen::Cursor::Shape cursor_shape,
                       int column_offset) {
  output += CursorPosition(cursor_x, cursor_y, width, height, column_offset);
  if (cursor_shape == Screen::Cursor::Hidden) {
    output += "\x1B[?25l";
    return;
  }
  output += "\x1B[?25h";
  output += "\x1B[" + std::to_string(static_cast<int>(cursor_shape)) + " q";
}

void AppendLineTail(std::string& output, bool erase_line_tail) {
  if (erase_line_tail) {
    // A guarded GNU Screen canvas stops one cell before the physical right
    // margin. Erase that reserved cell without printing into it, so Screen's
    // outer redisplay cannot trigger an eager-wrap + CRLF double advance.
    output += kResetStyleAndEraseLineTail;
  }
}

void AppendRowBody(std::string& output,
                   std::string_view content,
                   bool erase_line_tail,
                   bool blank_left_column) {
  if (blank_left_column) {
    // The left mirror column is not on the wrap-ambiguous right margin, so a
    // default-styled blank cell is printed directly. Printing it on every
    // emitted row also repaints the column after multiplexer redisplays.
    output += kResetStyleAndBlankLeftColumn;
  }
  output += content;
  AppendLineTail(output, erase_line_tail);
}

void AppendRow(std::string& output,
               std::size_t row,
               std::string_view content,
               bool erase_line_tail,
               bool blank_left_column) {
  output += "\x1B[" + std::to_string(row + 1) + ";1H";
  AppendRowBody(output, content, erase_line_tail, blank_left_column);
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
    FullscreenPresentMode mode,
    bool erase_line_tail,
    bool blank_left_column) {
  std::string output;
  output += kBeginSynchronizedOutput;
  // Hiding the cursor also prevents visible cursor sweeps on terminals that
  // safely ignore the synchronized-output private mode.
  output += kHideCursor;
  // Reassert DECAWM for every frame. GNU Screen can restore its virtual
  // terminal's wrapping mode across resize and redisplay paths without
  // restarting the application; a full-width update on the bottom row would
  // then scroll the window and leave the presenter's retained rows out of sync
  // with what is visible.
  output += kDisableLineWrap;

  const bool row_count_changed =
      previous_rows.size() != current_rows.size();
  if (mode == FullscreenPresentMode::FullRepaint) {
    // Multiplexers can invalidate a retained framebuffer when they reflow or
    // redisplay a window. Rewrite every physical row by absolute position so
    // the next frame is self-healing without exposing a blank screen between
    // frames. Avoid CRLF: some nested PTY chains apply delayed-wrap semantics
    // differently and can otherwise shift all rows below a full-width line.
    for (std::size_t row = 0; row < current_rows.size(); ++row) {
      AppendRow(
          output, row, current_rows[row], erase_line_tail, blank_left_column);
    }
  } else if (mode == FullscreenPresentMode::FullClear || row_count_changed) {
    output += "\x1B[2J\x1B[H";
    if (!current_rows.empty()) {
      AppendRowBody(
          output, current_rows[0], erase_line_tail, blank_left_column);
    }
    for (std::size_t row = 1; row < current_rows.size(); ++row) {
      AppendRow(
          output, row, current_rows[row], erase_line_tail, blank_left_column);
    }
  } else {
    for (std::size_t row = 0; row < current_rows.size(); ++row) {
      if (current_rows[row] == previous_rows[row]) {
        continue;
      }
      AppendRow(
          output, row, current_rows[row], erase_line_tail, blank_left_column);
    }
  }

  AppendCursorState(
      output,
      cursor_x,
      cursor_y,
      width,
      height,
      cursor_shape,
      blank_left_column ? 1 : 0);
  output += kEndSynchronizedOutput;
  return output;
}

}  // namespace ftxui::detail
