// Copyright 2020 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#include <algorithm>  // for min, max
#include <cstddef>
#include <memory>       // for make_shared
#include <string>       // for string, wstring
#include <string_view>  // for string_view
#include <utility>      // for move
#include <vector>       // for vector

#include "ftxui/dom/deprecated.hpp"   // for text, vtext
#include "ftxui/dom/elements.hpp"     // for Element, text, vtext
#include "ftxui/dom/node.hpp"         // for Node
#include "ftxui/dom/requirement.hpp"  // for Requirement
#include "ftxui/dom/selection.hpp"    // for Selection
#include "ftxui/screen/box.hpp"       // for Box
#include "ftxui/screen/screen.hpp"    // for Cell, Screen
#include "ftxui/screen/string.hpp"  // for string_width, Utf8ToGlyphs, to_string

namespace ftxui {

namespace {
using ftxui::Screen;

class Text : public Node {
 public:
  explicit Text(std::string_view text) : glyphs_(Utf8ToGlyphs(text)) {
    int max_width = 0;
    int current_width = 0;
    int lines_count = 1;
    lines_offsets_.push_back(0);

    for (size_t i = 0; i < glyphs_.size(); ++i) {
      if (glyphs_[i] == "\n") {
        max_width = std::max(max_width, current_width);
        current_width = 0;
        lines_count++;
        lines_offsets_.push_back((int)i + 1);
      } else {
        current_width++;
      }
    }
    max_width = std::max(max_width, current_width);
    lines_offsets_.push_back((int)glyphs_.size() + 1);

    requirement_.min_x = max_width;
    requirement_.min_y = lines_count;
  }

  void ComputeRequirement() override {
    // The requirement was computed once in the constructor. This hook still
    // runs before every frame; use it to clear the selection, which Select()
    // re-populates while a selection is active.
    selection_rows_.clear();
  }

  void Select(Selection& selection) override {
    const Box selection_box = Box::Intersection(selection.GetBox(), box_);
    if (selection_box.IsEmpty()) {
      return;
    }

    // Only store the selected line range. Sizing per line would allocate one
    // entry per line of the whole text on every frame.
    const size_t lines_count = lines_offsets_.size() - 1;
    const size_t first = selection_box.y_min - box_.y_min;
    const size_t last =
        std::min<size_t>(selection_box.y_max - box_.y_min + 1, lines_count);
    if (first >= last) {
      return;
    }
    selection_first_line_ = first;
    selection_rows_.assign(last - first, {-1, -1});

    for (size_t i = first; i < last; ++i) {
      const int y = box_.y_min + (int)i;
      const Box row_box{box_.x_min, box_.x_max, y, y};
      const Selection row_sel = selection.SaturateHorizontal(row_box);
      const int sel_start = row_sel.GetBox().x_min;
      const int sel_end = row_sel.GetBox().x_max;
      selection_rows_[i - first] = {sel_start, sel_end};

      std::string part;
      int x = box_.x_min;
      const int start = lines_offsets_[i];
      const int end = lines_offsets_[i + 1] - 1;
      for (int j = start; j < end; ++j) {
        // Utf8ToGlyphs emits one entry per cell: a full-width glyph is
        // followed by an empty placeholder holding its second cell. Advance
        // the column by the glyph's actual width, and select a glyph as a
        // whole when the selection covers any cell it occupies, so a
        // selection starting on the second half of a CJK glyph does not
        // skip it.
        const int width = string_width(glyphs_[j]);
        if (width > 0 && x <= sel_end && sel_start < x + width) {
          part += glyphs_[j];
        }
        x += width;
      }
      selection.AddPart(std::move(part), y, sel_start, sel_end);
    }
  }

  void Render(Screen& screen) override {
    const auto visible_box = Box::Intersection(screen.stencil, box_);
    if (visible_box.IsEmpty()) {
      return;
    }

    int y = visible_box.y_min;

    const size_t first_line = visible_box.y_min - box_.y_min;
    const size_t last_line = std::min<size_t>(
        visible_box.y_max - box_.y_min + 1, lines_offsets_.size() - 1);

    for (size_t line = first_line; line < last_line; ++line, ++y) {
      int x = box_.x_min;

      for (auto glyph = glyphs_.begin() + lines_offsets_[line];
           glyph != glyphs_.end() && *glyph != "\n"; ++glyph) {
        // A full-width glyph is followed by an empty placeholder entry for
        // its second cell. Handle both cells as one unit so the glyph is
        // inverted as a whole and a selection can not cut it in half.
        const int width = string_width(*glyph);
        if (width == 0) {
          continue;
        }
        if (x > box_.x_max) {
          break;
        }

        const size_t sel_index = line - selection_first_line_;
        bool selected = false;
        if (sel_index < selection_rows_.size()) {
          const auto& [sel_start, sel_end] = selection_rows_[sel_index];
          selected = sel_start != -1 && x <= sel_end && sel_start < x + width;
        }

        for (int dx = 0; dx < width && x + dx <= box_.x_max; ++dx) {
          auto& cell = screen.CellAt(x + dx, y);
          if (dx == 0) {
            cell.character = *glyph;
          } else {
            // Reserve the second cell of a full-width glyph.
            cell.character = "";
          }
          if (selected) {
            screen.GetSelectionStyle()(cell);
          }
        }
        x += width;
      }
    }
  }

 private:
  std::vector<std::string> glyphs_;
  std::vector<int> lines_offsets_;
  // Selection state for the line range [selection_first_line_,
  // selection_first_line_ + selection_rows_.size()).
  size_t selection_first_line_ = 0;
  std::vector<std::pair<int, int>> selection_rows_;
};

class VText : public Node {
 public:
  explicit VText(std::string_view text) : glyphs_(Utf8ToGlyphs(text)) {
    for (const auto& g : glyphs_) {
      if (g != "\n") {
        width_ = 1;
        break;
      }
    }
  }

  void ComputeRequirement() override {
    int max_height = 0;
    int current_height = 0;
    int columns = 1;

    for (const auto& cell : glyphs_) {
      if (cell == "\n") {
        max_height = std::max(max_height, current_height);
        current_height = 0;
        columns++;
      } else {
        current_height++;
      }
    }
    max_height = std::max(max_height, current_height);

    requirement_.min_x = width_ * columns;
    requirement_.min_y = max_height;
  }

  void Render(Screen& screen) override {
    int x = box_.x_min;
    int y = box_.y_min;
    if (x + width_ - 1 > box_.x_max) {
      return;
    }
    for (const auto& it : glyphs_) {
      if (it == "\n") {
        x += width_;
        y = box_.y_min;
        if (x + width_ - 1 > box_.x_max) {
          return;
        }
        continue;
      }
      if (y > box_.y_max) {
        continue;
      }
      screen.CellAt(x, y).character = it;
      y += 1;
    }
  }

 private:
  std::vector<std::string> glyphs_;
  int width_ = 0;
};

}  // namespace

/// @brief Display a piece of UTF8 encoded unicode text.
/// @ingroup dom
/// @see ftxui::to_wstring
///
/// ### Example
///
/// ```cpp
/// Element document = text("Hello world!");
/// ```
///
/// ### Output
///
/// ```bash
/// Hello world!
/// ```
Element text(std::string_view text) {
  return std::make_shared<Text>(std::string(text));
}

/// @brief Display a piece of unicode text.
/// @ingroup dom
/// @see ftxui::to_wstring
///
/// ### Example
///
/// ```cpp
/// Element document = text(L"Hello world!");
/// ```
///
/// ### Output
///
/// ```bash
/// Hello world!
/// ```
Element text(std::wstring_view text) {
  return ftxui::text(to_string(text));
}

/// @brief Display a piece of unicode text vertically.
/// @ingroup dom
/// @see ftxui::to_wstring
///
/// ### Example
///
/// ```cpp
/// Element document = vtext("Hello world!");
/// ```
///
/// ### Output
///
/// ```bash
/// H
/// e
/// l
/// l
/// o
///
/// w
/// o
/// r
/// l
/// d
/// !
/// ```
Element vtext(std::string_view text) {
  return std::make_shared<VText>(text);
}

/// @brief Display a piece unicode text vertically.
/// @ingroup dom
/// @see ftxui::to_wstring
///
/// ### Example
///
/// ```cpp
/// Element document = vtext(L"Hello world!");
/// ```
///
/// ### Output
///
/// ```bash
/// H
/// e
/// l
/// l
/// o
///
/// w
/// o
/// r
/// l
/// d
/// !
/// ```
Element vtext(std::wstring_view text) {  // NOLINT
  return vtext(to_string(text));
}

}  // namespace ftxui
