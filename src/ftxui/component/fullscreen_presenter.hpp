// Local gclaw extension for FTXUI's fullscreen alternate-screen presenter.
#ifndef FTXUI_COMPONENT_FULLSCREEN_PRESENTER_HPP
#define FTXUI_COMPONENT_FULLSCREEN_PRESENTER_HPP

#include <string>
#include <string_view>
#include <vector>

#include "ftxui/screen/screen.hpp"

namespace ftxui::detail {

/// Split a Screen::ToString() framebuffer into independently styled rows.
std::vector<std::string> SplitFullscreenRows(std::string_view frame);

/// Build one atomic fullscreen update, emitting only rows that changed.
std::string PresentFullscreenRows(
    const std::vector<std::string>& previous_rows,
    const std::vector<std::string>& current_rows,
    int width,
    int height,
    int cursor_x,
    int cursor_y,
    Screen::Cursor::Shape cursor_shape,
    bool force_full);

}  // namespace ftxui::detail

#endif  // FTXUI_COMPONENT_FULLSCREEN_PRESENTER_HPP
