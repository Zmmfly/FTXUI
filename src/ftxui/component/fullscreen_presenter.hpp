// Fullscreen alternate-screen differential presenter for FTXUI.
#ifndef FTXUI_COMPONENT_FULLSCREEN_PRESENTER_HPP
#define FTXUI_COMPONENT_FULLSCREEN_PRESENTER_HPP

#include <string>
#include <string_view>
#include <vector>

#include "ftxui/screen/screen.hpp"

namespace ftxui::detail {

enum class FullscreenPresentMode {
  Differential,
  FullClear,
  FullRepaint,
};

/// Split a Screen::ToString() framebuffer into independently styled rows.
std::vector<std::string> SplitFullscreenRows(std::string_view frame);

/// Build one atomic fullscreen update using the requested recovery strategy.
std::string PresentFullscreenRows(
    const std::vector<std::string>& previous_rows,
    const std::vector<std::string>& current_rows,
    int width,
    int height,
    int cursor_x,
    int cursor_y,
    Screen::Cursor::Shape cursor_shape,
    FullscreenPresentMode mode,
    bool erase_line_tail = false);

}  // namespace ftxui::detail

#endif  // FTXUI_COMPONENT_FULLSCREEN_PRESENTER_HPP
