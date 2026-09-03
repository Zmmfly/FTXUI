// Copyright 2022 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#include <algorithm>   // for max, min
#include <cstddef>     // for size_t
#include <cstdint>     // for uint32_t
#include <functional>  // for function
#include <sstream>     // for basic_istream, stringstream
#include <string>      // for string, basic_string, operator==, getline
#include <utility>     // for move
#include <vector>      // for vector

#include "ftxui/component/app.hpp"                // for Component
#include "ftxui/component/component.hpp"          // for Make, Input
#include "ftxui/component/component_base.hpp"     // for ComponentBase
#include "ftxui/component/component_options.hpp"  // for InputOption
#include "ftxui/component/detail/backspace_accelerator.hpp"
#include "ftxui/component/event.hpp"  // for Event, Event::ArrowDown, Event::ArrowLeft, Event::ArrowLeftCtrl, Event::ArrowRight, Event::ArrowRightCtrl, Event::ArrowUp, Event::Backspace, Event::Delete, Event::End, Event::Home, Event::Return
#include "ftxui/component/mouse.hpp"  // for Mouse, Mouse::Left, Mouse::Pressed
#include "ftxui/dom/elements.hpp"  // for operator|, reflect, text, Element, xflex, hbox, Elements, frame, operator|=, vbox, focus, focusCursorBarBlinking, select
#include "ftxui/screen/box.hpp"    // for Box
#include "ftxui/screen/string.hpp"           // for string_width
#include "ftxui/screen/string_internal.hpp"  // for GlyphNext, GlyphPrevious, WordBreakProperty, EatCodePoint, CodepointToWordBreakProperty, IsFullWidth, WordBreakProperty::ALetter, WordBreakProperty::CR, WordBreakProperty::Double_Quote, WordBreakProperty::Extend, WordBreakProperty::ExtendNumLet, WordBreakProperty::Format, WordBreakProperty::Hebrew_Letter, WordBreakProperty::Katakana, WordBreakProperty::LF, WordBreakProperty::MidLetter, WordBreakProperty::MidNum, WordBreakProperty::MidNumLet, WordBreakProperty::Newline, WordBreakProperty::Numeric, WordBreakProperty::Regional_Indicator, WordBreakProperty::Single_Quote, WordBreakProperty::WSegSpace, WordBreakProperty::ZWJ
#include "ftxui/screen/util.hpp"             // for clamp
#include "ftxui/util/ref.hpp"                // for StringRef, Ref

namespace ftxui {

namespace {

std::vector<std::string> SplitLines(std::string_view input) {
  std::vector<std::string> output;
  size_t start = 0;
  size_t end = input.find('\n');
  while (end != std::string_view::npos) {
    output.push_back(std::string(input.substr(start, end - start)));
    start = end + 1;
    end = input.find('\n', start);
  }
  output.push_back(std::string(input.substr(start)));
  return output;
}

size_t GlyphWidth(std::string_view input, size_t iter) {
  uint32_t ucs = 0;
  if (!EatCodePoint(input, iter, &iter, &ucs)) {
    return 0;
  }
  if (IsFullWidth(ucs)) {
    return 2;
  }
  return 1;
}

// Soft-wrap a single logical line (no embedded '\n') into visual-line byte
// ranges [start, end) so that every visual line's display width stays within
// |max_width| cells. Full-width glyphs are never split: when one would
// overflow the remaining width it starts a new visual line. |max_width| <= 0
// disables wrapping and returns the whole line as a single range, which keeps
// the original horizontal-scroll behavior intact.
std::vector<std::pair<size_t, size_t>> WrapLineGlyphs(const std::string& line,
                                                      int max_width) {
  std::vector<std::pair<size_t, size_t>> ranges;
  if (max_width <= 0) {
    ranges.emplace_back(0, line.size());
    return ranges;
  }
  size_t start = 0;
  int width = 0;
  size_t iter = 0;
  while (iter < line.size()) {
    const int glyph_width = static_cast<int>(GlyphWidth(line, iter));
    const size_t next = GlyphNext(line, iter);
    // Starting a fresh visual line at width == 0 is always allowed, so a
    // glyph wider than the budget still lands on its own line.
    if (width + glyph_width > max_width && width > 0) {
      ranges.emplace_back(start, iter);
      start = iter;
      width = 0;
    }
    width += glyph_width;
    iter = next;
  }
  ranges.emplace_back(start, line.size());
  return ranges;
}

// Location of the cursor within the soft-wrap layout: the byte offset (in
// |content|) of the visual line that owns the cursor, and the cursor's
// display column measured from that visual line's start.
struct VisualCursorPlace {
  size_t visual_start;
  int column;
};

// Resolve the cursor's visual line for the given |content|/|cursor|/wrap
// budget. A cursor sitting exactly on a wrap boundary attaches to the earlier
// visual line's end (matching how OnRender places the cursor element).
VisualCursorPlace LocateVisualCursor(const std::string& content,
                                     size_t cursor,
                                     int wrap_width) {
  size_t line_start = cursor;
  while (line_start > 0 && content[line_start - 1] != '\n') {
    --line_start;
  }
  size_t line_end = cursor;
  while (line_end < content.size() && content[line_end] != '\n') {
    ++line_end;
  }
  const std::string line = content.substr(line_start, line_end - line_start);
  const size_t local_cursor = cursor - line_start;
  const auto ranges = WrapLineGlyphs(line, wrap_width);
  size_t chosen = ranges.size() - 1;
  for (size_t r = 0; r < ranges.size(); ++r) {
    if (local_cursor <= ranges[r].second) {
      chosen = r;
      break;
    }
  }
  const size_t visual_start_local = ranges[chosen].first;
  int column = 0;
  for (size_t it = visual_start_local; it < local_cursor;) {
    column += static_cast<int>(GlyphWidth(line, it));
    it = GlyphNext(line, it);
  }
  return {line_start + visual_start_local, column};
}

bool IsWordCodePoint(uint32_t codepoint) {
  switch (CodepointToWordBreakProperty(codepoint)) {
    case WordBreakProperty::ALetter:
    case WordBreakProperty::Hebrew_Letter:
    case WordBreakProperty::Katakana:
    case WordBreakProperty::Numeric:
      return true;

    case WordBreakProperty::CR:
    case WordBreakProperty::Double_Quote:
    case WordBreakProperty::LF:
    case WordBreakProperty::MidLetter:
    case WordBreakProperty::MidNum:
    case WordBreakProperty::MidNumLet:
    case WordBreakProperty::Newline:
    case WordBreakProperty::Single_Quote:
    case WordBreakProperty::WSegSpace:
    // Unexpected/Unsure
    case WordBreakProperty::Extend:
    case WordBreakProperty::ExtendNumLet:
    case WordBreakProperty::Format:
    case WordBreakProperty::Regional_Indicator:
    case WordBreakProperty::ZWJ:
      return false;
  }
  return false;  // NOT_REACHED();
}

bool IsWordCharacter(std::string_view input, size_t iter) {
  uint32_t ucs = 0;
  if (!EatCodePoint(input, iter, &iter, &ucs)) {
    return false;
  }

  return IsWordCodePoint(ucs);
}

// An input box. The user can type text into it.
class InputBase : public ComponentBase, public InputOption {
 public:
  // NOLINTNEXTLINE
  InputBase(InputOption option) : InputOption(std::move(option)) {}

 private:
  // Component implementation:
  Element OnRender() override {
    const bool is_focused = Focused();
    const auto focused = (!is_focused && !hovered_) ? focus
                         : insert()                 ? focusCursorBarBlinking
                                                    : focusCursorBlockBlinking;
    const auto decorate_cursor = [&](Element element) {
      element = focused(std::move(element));
      if (is_focused && cursor_cell_inverted()) {
        element |= inverted;
      }
      return element;
    };

    auto transform_func =
        transform ? transform : InputOption::Default().transform;

    // placeholder.
    if (content->empty()) {
      // The placeholder row is a hint, not content: keep both the hint
      // text and the focus cell out of screen text selections so copying
      // an empty input cannot pick them up.
      auto element =
          vbox({
              unselectable(dbox({
                  hbox({
                      decorate_cursor(text(" ")) | reflect(cursor_box_),
                      text("") | xflex,
                  }),
                  text(placeholder()),
              })),
          }) |
          xflex | frame;

      return transform_func({
                 std::move(element), hovered_, is_focused,
                 true  // placeholder
             }) |
             reflect(box_);
    }

    Elements elements;
    const std::vector<std::string> lines = SplitLines(*content);

    cursor_position() = util::clamp(cursor_position(), 0, (int)content->size());

    // Refresh the soft-wrap budget from the previous frame's box. The frame
    // fills box_ during Render, so OnRender observes last frame's geometry;
    // the first frame (or the one right after a resize) falls back to no
    // wrapping and self-corrects on the next frame. One cell is reserved so
    // the trailing cursor placeholder never pushes a visual line past the
    // frame width and re-triggers horizontal scrolling.
    wrap_width_ = 0;
    if (wrap_text()) {
      const int available = box_.x_max - box_.x_min + 1;
      if (available > 1) {
        wrap_width_ = available - 1;
      }
    }

    // Find the line and index of the cursor.
    int cursor_line = 0;
    int cursor_char_index = cursor_position();
    for (const auto& line : lines) {
      if (cursor_char_index <= (int)line.size()) {
        break;
      }

      cursor_char_index -= static_cast<int>(line.size() + 1);
      cursor_line++;
    }

    if (lines.empty()) {
      elements.push_back(text("") | focused);
    }

    // Lay out every logical line, optionally split into width-bounded visual
    // lines. With wrapping disabled (wrap_width_ <= 0) each logical line yields
    // a single visual line and the original horizontal-scroll behavior is
    // preserved; with wrapping enabled every visual line fits the frame, so
    // the frame only ever scrolls vertically.
    int visual_line_index = 0;
    bool cursor_placed = false;
    elements.reserve(lines.size());
    for (size_t i = 0; i < lines.size(); ++i) {
      const std::string& line = lines[i];
      const bool cursor_on_logical_line = int(i) == cursor_line;

      for (const auto& range : WrapLineGlyphs(line, wrap_width_)) {
        const size_t vstart = range.first;
        const size_t vend = range.second;

        // The cursor attaches to the first visual line whose end reaches it,
        // so a cursor on a wrap boundary sits at the earlier line's end.
        const bool cursor_here = cursor_on_logical_line && !cursor_placed &&
                                 cursor_char_index <= static_cast<int>(vend);
        if (!cursor_here) {
          elements.push_back(Text(line.substr(vstart, vend - vstart)));
          ++visual_line_index;
          continue;
        }

        cursor_placed = true;

        Element cursor_element;
        if (cursor_char_index >= static_cast<int>(vend) ||
            cursor_char_index >= static_cast<int>(line.size())) {
          // Cursor at the end of this visual line.
          cursor_element = hbox({
              Text(line.substr(vstart, vend - vstart)),
              decorate_cursor(text(" ")) | reflect(cursor_box_),
          });
        } else {
          // Cursor in the middle of this visual line.
          const int glyph_end =
              static_cast<int>(GlyphNext(line, cursor_char_index));
          cursor_element = hbox({
              Text(line.substr(vstart, static_cast<size_t>(cursor_char_index) -
                                           vstart)),
              decorate_cursor(Text(line.substr(
                  static_cast<size_t>(cursor_char_index),
                  static_cast<size_t>(glyph_end) -
                      static_cast<size_t>(cursor_char_index)))) |
                  reflect(cursor_box_),
              Text(line.substr(static_cast<size_t>(glyph_end), vend - glyph_end)),
          });
        }
        // Without wrapping the cursor line stretches to fill the frame (the
        // original behavior); with wrapping every visual line is width-bound
        // so the trailing cursor cell never triggers horizontal scrolling.
        if (wrap_width_ <= 0) {
          cursor_element = std::move(cursor_element) | xflex;
        }
        elements.push_back(std::move(cursor_element));
        ++visual_line_index;
      }
    }

    // Upstream v6 passed the cursor line to vbox as a second argument that
    // its generic Merge silently discarded; v7 removed that no-op overload,
    // so the call keeps the plain single-argument form.
    auto element = vbox(std::move(elements)) | frame;
    return transform_func({
               std::move(element), hovered_, is_focused,
               false  // placeholder
           }) |
           xflex | reflect(box_);
  }

  Element Text(const std::string& input) {
    if (!password()) {
      return text(input);
    }

    const size_t glyph_count = GlyphCount(input);
    std::string out;
    out.reserve(glyph_count * 3);
    for (size_t i = 0; i < glyph_count; ++i) {
      out += "•";
    }
    return text(out);
  }

  bool HandleBackspace() {
    if (cursor_position() == 0) {
      backspace_accelerator_.Reset();
      return false;
    }

    size_t glyphs = backspace_accelerator_.OnBackspace(
        detail::BackspaceAccelerator::Clock::now());
    do {
      const size_t start = GlyphPrevious(content(), cursor_position());
      const size_t end = cursor_position();
      content->erase(start, end - start);
      cursor_position() = static_cast<int>(start);
      --glyphs;
    } while (glyphs != 0 && cursor_position() != 0);

    App::PostEventOrExecute(on_change);
    return true;
  }

  bool DeleteImpl() {
    if (cursor_position() == (int)content->size()) {
      return false;
    }
    const size_t start = cursor_position();
    const size_t end = GlyphNext(content(), cursor_position());
    content->erase(start, end - start);
    return true;
  }

  bool HandleDelete() {
    if (DeleteImpl()) {
      App::PostEventOrExecute(on_change);
      return true;
    }
    return false;
  }

  bool HandleArrowLeft() {
    if (cursor_position() == 0) {
      return false;
    }

    cursor_position() =
        static_cast<int>(GlyphPrevious(content(), cursor_position()));
    return true;
  }

  bool HandleArrowRight() {
    if (cursor_position() == (int)content->size()) {
      return false;
    }

    cursor_position() =
        static_cast<int>(GlyphNext(content(), cursor_position()));
    return true;
  }

  size_t CursorColumn() {
    size_t iter = cursor_position();
    int width = 0;
    while (true) {
      if (iter == 0) {
        break;
      }
      iter = GlyphPrevious(content(), iter);
      if (content()[iter] == '\n') {
        break;
      }
      if (password()) {
        width += 1;
      } else {
        width += static_cast<int>(GlyphWidth(content(), iter));
      }
    }
    return width;
  }

  // Move the cursor `columns` on the right, if possible.
  void MoveCursorColumn(int columns) {
    while (columns > 0) {
      if (cursor_position() == (int)content().size() ||
          content()[cursor_position()] == '\n') {
        return;
      }

      if (password()) {
        columns -= 1;
      } else {
        columns -= static_cast<int>(GlyphWidth(content(), cursor_position()));
      }
      cursor_position() =
          static_cast<int>(GlyphNext(content(), cursor_position()));
    }
  }

  bool HandleArrowUp() {
    if (cursor_position() == 0) {
      return false;
    }

    // Soft-wrap: step across visual lines rather than whole logical lines.
    if (wrap_width_ > 0) {
      const VisualCursorPlace place = LocateVisualCursor(
          content(), cursor_position(), wrap_width_);
      const int target_column = place.column;
      const size_t vstart = place.visual_start;
      if (vstart == 0) {
        cursor_position() = 0;
        return true;
      }
      size_t previous = GlyphPrevious(content(), vstart);
      // Step over the newline that separates two logical lines.
      if (content()[previous] == '\n') {
        if (previous == 0) {
          cursor_position() = 0;
          return true;
        }
        previous = GlyphPrevious(content(), previous);
      }
      const VisualCursorPlace prev_place =
          LocateVisualCursor(content(), previous, wrap_width_);
      cursor_position() = static_cast<int>(prev_place.visual_start);
      MoveCursorColumn(target_column);
      return true;
    }

    const size_t columns = CursorColumn();

    // Move cursor at the beginning of 2 lines above.
    while (true) {
      if (cursor_position() == 0) {
        return true;
      }
      const size_t previous = GlyphPrevious(content(), cursor_position());
      if (content()[previous] == '\n') {
        break;
      }
      cursor_position() = static_cast<int>(previous);
    }
    cursor_position() =
        static_cast<int>(GlyphPrevious(content(), cursor_position()));
    while (true) {
      if (cursor_position() == 0) {
        break;
      }
      const size_t previous = GlyphPrevious(content(), cursor_position());
      if (content()[previous] == '\n') {
        break;
      }
      cursor_position() = static_cast<int>(previous);
    }

    MoveCursorColumn(static_cast<int>(columns));
    return true;
  }

  bool HandleArrowDown() {
    if (cursor_position() == (int)content->size()) {
      return false;
    }

    // Soft-wrap: step across visual lines rather than whole logical lines.
    if (wrap_width_ > 0) {
      const VisualCursorPlace place = LocateVisualCursor(
          content(), cursor_position(), wrap_width_);
      const int target_column = place.column;
      // Walk right from the current visual-line start until the width budget
      // is exhausted; the next glyph begins the following visual line.
      size_t it = place.visual_start;
      int width = 0;
      while (it < content().size() && content()[it] != '\n') {
        const int gw = static_cast<int>(GlyphWidth(content(), it));
        if (width + gw > wrap_width_) {
          break;
        }
        width += gw;
        it = GlyphNext(content(), it);
      }
      if (it >= content().size()) {
        cursor_position() = static_cast<int>(content().size());
        return true;
      }
      if (content()[it] == '\n') {
        it = GlyphNext(content(), it);
      }
      if (it >= content().size()) {
        cursor_position() = static_cast<int>(content().size());
        return true;
      }
      cursor_position() = static_cast<int>(it);
      MoveCursorColumn(target_column);
      return true;
    }

    const size_t columns = CursorColumn();

    // Move cursor at the beginning of the next line
    while (true) {
      if (content()[cursor_position()] == '\n') {
        break;
      }
      cursor_position() =
          static_cast<int>(GlyphNext(content(), cursor_position()));
      if (cursor_position() == (int)content().size()) {
        return true;
      }
    }
    cursor_position() =
        static_cast<int>(GlyphNext(content(), cursor_position()));

    MoveCursorColumn(static_cast<int>(columns));
    return true;
  }

  bool HandleHome() {
    // Move to the start of the current logical line: stop right after the
    // previous '\n', or at the content start. '\n' is a single-byte ASCII
    // separator, so a raw byte scan needs no glyph alignment.
    size_t position = cursor_position();
    while (position > 0 && content()[position - 1] != '\n') {
      --position;
    }
    cursor_position() = static_cast<int>(position);
    return true;
  }

  bool HandleEnd() {
    // Move to the end of the current logical line: stop right before the next
    // '\n', or at the content end. Soft-wrap boundaries are not line ends.
    size_t position = cursor_position();
    while (position < content().size() && content()[position] != '\n') {
      ++position;
    }
    cursor_position() = static_cast<int>(position);
    return true;
  }

  bool HandleReturn() {
    if (multiline()) {
      HandleCharacter("\n");
    }
    App::PostEventOrExecute(on_enter);
    return true;
  }

  bool HandleCharacter(const std::string& character) {
    if (!insert() && cursor_position() < (int)content->size() &&
        content()[cursor_position()] != '\n') {
      DeleteImpl();
    }
    content->insert(cursor_position(), character);
    cursor_position() += static_cast<int>(character.size());
    App::PostEventOrExecute(on_change);
    return true;
  }

  bool OnEvent(Event event) override {
    cursor_position() = util::clamp(cursor_position(), 0, (int)content->size());

    if (detail::ResetsBackspaceAcceleration(event)) {
      backspace_accelerator_.Reset();
    }
    if (event.is_character()) {
      return HandleCharacter(event.character());
    }
    if (event == Event::Return) {
      return HandleReturn();
    }
    if (event.is_mouse()) {
      return HandleMouse(event);
    }
    if (event == Event::Backspace) {
      return HandleBackspace();
    }
    if (event == Event::Delete) {
      return HandleDelete();
    }
    if (event == Event::ArrowLeft) {
      return HandleArrowLeft();
    }
    if (event == Event::ArrowRight) {
      return HandleArrowRight();
    }
    if (event == Event::ArrowUp) {
      return HandleArrowUp();
    }
    if (event == Event::ArrowDown) {
      return HandleArrowDown();
    }
    if (event == Event::Home) {
      return HandleHome();
    }
    if (event == Event::End) {
      return HandleEnd();
    }
    if (event == Event::ArrowLeftCtrl) {
      return HandleLeftCtrl();
    }
    if (event == Event::ArrowRightCtrl) {
      return HandleRightCtrl();
    }
    if (event == Event::Insert) {
      return HandleInsert();
    }
    return false;
  }

  bool HandleLeftCtrl() {
    if (cursor_position() == 0) {
      return false;
    }

    // Move left, as long as left it not a word.
    while (cursor_position()) {
      const size_t previous = GlyphPrevious(content(), cursor_position());
      if (IsWordCharacter(content(), previous)) {
        break;
      }
      cursor_position() = static_cast<int>(previous);
    }
    // Move left, as long as left is a word character:
    while (cursor_position()) {
      const size_t previous = GlyphPrevious(content(), cursor_position());
      if (!IsWordCharacter(content(), previous)) {
        break;
      }
      cursor_position() = static_cast<int>(previous);
    }
    return true;
  }

  bool HandleRightCtrl() {
    if (cursor_position() == (int)content().size()) {
      return false;
    }

    // Move right, until entering a word.
    while (cursor_position() < (int)content().size()) {
      cursor_position() =
          static_cast<int>(GlyphNext(content(), cursor_position()));
      if (IsWordCharacter(content(), cursor_position())) {
        break;
      }
    }
    // Move right, as long as right is a word character:
    while (cursor_position() < (int)content().size()) {
      const size_t next = GlyphNext(content(), cursor_position());
      if (!IsWordCharacter(content(), cursor_position())) {
        break;
      }
      cursor_position() = static_cast<int>(next);
    }

    return true;
  }

  // Map a mouse event inside the box onto the caret position. Shared by
  // release-time click placement; the caller checks containment.
  void PlaceCursorFromMouse(Event event) {
    if (content->empty()) {
      cursor_position() = 0;
      return;
    }

    // Soft-wrap: map the click onto the visual-line layout instead of the
    // logical-line layout, so a click lands on the wrapped row the user
    // sees.
    if (wrap_width_ > 0) {
      std::vector<std::pair<size_t, size_t>> vlines;
      size_t base = 0;
      for (const auto& logical : SplitLines(*content)) {
        for (const auto& r : WrapLineGlyphs(logical, wrap_width_)) {
          vlines.emplace_back(base + r.first, base + r.second);
        }
        base += logical.size() + 1;
      }
      if (vlines.empty()) {
        vlines.emplace_back(0, 0);
      }

      // The cursor box anchors a visible visual line; offset from it by
      // the click's vertical delta to pick the target visual line.
      int cursor_vline = static_cast<int>(vlines.size()) - 1;
      for (size_t i = 0; i < vlines.size(); ++i) {
        if (cursor_position() <= static_cast<int>(vlines[i].second)) {
          cursor_vline = static_cast<int>(i);
          break;
        }
      }
      int target_vline =
          cursor_vline + (event.mouse().y - cursor_box_.y_min);
      target_vline =
          util::clamp(target_vline, 0, static_cast<int>(vlines.size()) - 1);

      const auto& vl = vlines[target_vline];
      const std::string seg =
          content().substr(vl.first, vl.second - vl.first);
      const int vl_width = static_cast<int>(string_width(seg));
      int target_column = event.mouse().x - box_.x_min;
      target_column = util::clamp(target_column, 0, vl_width);

      cursor_position() = static_cast<int>(vl.first);
      int col = target_column;
      while (col > 0 &&
             cursor_position() < static_cast<int>(content->size()) &&
             content()[cursor_position()] != '\n') {
        col -= static_cast<int>(GlyphWidth(content(), cursor_position()));
        if (col < 0) {
          break;  // Keep a full-width glyph whole; stop just before it.
        }
        cursor_position() =
            static_cast<int>(GlyphNext(content(), cursor_position()));
      }
      on_change();
      return;
    }

    // Find the line and index of the cursor.
    std::vector<std::string> lines = SplitLines(*content);
    int cursor_line = 0;
    int cursor_char_index = cursor_position();
    for (const auto& line : lines) {
      if (cursor_char_index <= (int)line.size()) {
        break;
      }

      cursor_char_index -= static_cast<int>(line.size() + 1);
      cursor_line++;
    }
    const int cursor_column =
        password()
            ? GlyphCount(lines[cursor_line].substr(0, cursor_char_index))
            : string_width(lines[cursor_line].substr(0, cursor_char_index));

    int new_cursor_column =
        cursor_column + event.mouse().x - cursor_box_.x_min;
    int new_cursor_line = cursor_line + event.mouse().y - cursor_box_.y_min;

    // Fix the new cursor position:
    new_cursor_line = std::max(std::min(new_cursor_line, (int)lines.size()), 0);

    const std::string empty_string;
    const std::string& line = new_cursor_line < (int)lines.size()
                                  ? lines[new_cursor_line]
                                  : empty_string;
    new_cursor_column =
        util::clamp(new_cursor_column, 0,
                    password() ? GlyphCount(line) : string_width(line));

    if (new_cursor_column == cursor_column &&  //
        new_cursor_line == cursor_line) {
      return;
    }

    // Convert back the new_cursor_{line,column} toward cursor_position:
    cursor_position() = 0;
    for (int i = 0; i < new_cursor_line; ++i) {
      cursor_position() += static_cast<int>(lines[i].size() + 1);
    }
    while (new_cursor_column > 0) {
      if (password()) {
        new_cursor_column -= 1;
      } else {
        new_cursor_column -=
            static_cast<int>(GlyphWidth(content(), cursor_position()));
      }
      cursor_position() =
          static_cast<int>(GlyphNext(content(), cursor_position()));
    }

    App::PostEventOrExecute(on_change);
  }

  bool HandleMouse(Event event) {
    // Finish our own press sequence even when the pointer has left the
    // box: a drag ending outside must not strand the sequence state.
    if (m_mouse_press_active) {
      const auto& mouse = event.mouse();
      if (mouse.motion == Mouse::Released) {
        m_mouse_press_active = false;
        const bool dragged = m_mouse_dragged;
        m_mouse_dragged = false;
        // A plain click places the caret on release; a drag selection
        // leaves the caret where the user was editing.
        if (!dragged && box_.Contain(mouse.x, mouse.y)) {
          PlaceCursorFromMouse(event);
        }
        return false;
      }
      if (mouse.motion == Mouse::Moved && mouse.button == Mouse::Left) {
        m_mouse_dragged = true;
        return false;
      }
    }

    hovered_ = box_.Contain(event.mouse().x,  //
                            event.mouse().y) &&
               CaptureMouse(event);
    if (!hovered_) {
      return false;
    }

    if (event.mouse().button != Mouse::Left) {
      return false;
    }
    if (event.mouse().motion != Mouse::Pressed) {
      return false;
    }

    // A left press focuses this input but defers caret placement to the
    // release: a drag starts an App-level selection and must not move the
    // caret, and the press itself stays unconsumed so the selection can
    // start.
    TakeFocus();
    m_mouse_press_active = true;
    m_mouse_dragged = false;
    return false;
  }

  bool HandleInsert() {
    insert() = !insert();
    return true;
  }

  bool Focusable() const final { return true; }

  bool hovered_ = false;

  // Our own left-press sequence (press → optional drag → release). The
  // caret is placed only on a zero-drag release inside the box, so drag
  // selections never move the caret.
  bool m_mouse_press_active = false;
  bool m_mouse_dragged = false;

  detail::BackspaceAccelerator backspace_accelerator_;
  Box box_;
  Box cursor_box_;

  // Cached soft-wrap width (frame cells reserved for text) from the previous
  // frame; 0 disables wrapping. Refreshed every OnRender from box_, so the
  // first frame and the frame following a resize fall back to no wrapping.
  int wrap_width_ = 0;
};

}  // namespace

/// @brief An input box for editing text.
/// @param option Additional optional parameters.
/// @ingroup component
/// @see InputBase
///
/// ### Example
///
/// ```cpp
/// auto screen = App::FitComponent();
/// std::string content= "";
/// std::string placeholder = "placeholder";
/// Component input = Input({
///   .content = &content,
///   .placeholder = &placeholder,
/// })
/// screen.Loop(input);
/// ```
///
/// ### Output
///
/// ```bash
/// placeholder
/// ```
Component Input(InputOption option) {
  return Make<InputBase>(std::move(option));
}

/// @brief An input box for editing text.
/// @param content The editable content.
/// @param option Additional optional parameters.
/// @ingroup component
/// @see InputBase
///
/// ### Example
///
/// ```cpp
/// auto screen = App::FitComponent();
/// std::string content= "";
/// std::string placeholder = "placeholder";
/// Component input = Input(content, {
///   .placeholder = &placeholder,
///   .password = true,
/// })
/// screen.Loop(input);
/// ```
///
/// ### Output
///
/// ```bash
/// placeholder
/// ```
Component Input(StringRef content, InputOption option) {
  option.content = std::move(content);
  return Make<InputBase>(std::move(option));
}

/// @brief An input box for editing text.
/// @param content The editable content.
/// @param placeholder The placeholder text.
/// @param option Additional optional parameters.
/// @ingroup component
/// @see InputBase
///
/// ### Example
///
/// ```cpp
/// auto screen = App::FitComponent();
/// std::string content= "";
/// std::string placeholder = "placeholder";
/// Component input = Input(content, placeholder);
/// screen.Loop(input);
/// ```
///
/// ### Output
///
/// ```bash
/// placeholder
/// ```
Component Input(StringRef content, StringRef placeholder, InputOption option) {
  option.content = std::move(content);
  option.placeholder = std::move(placeholder);
  return Make<InputBase>(std::move(option));
}

}  // namespace ftxui
