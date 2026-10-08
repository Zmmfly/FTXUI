// Copyright 2024 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.

#include "ftxui/dom/selection.hpp"  // for Selection
#include <algorithm>                // for max, min
#include <memory>                   // for make_shared
#include <string_view>
#include <tuple>  // for ignore
#include <utility>  // for move

#include "ftxui/dom/node_decorator.hpp"  // for NodeDecorator

namespace ftxui {

namespace {
class Unselectable : public NodeDecorator {
 public:
  using NodeDecorator::NodeDecorator;

  void Select(Selection& ignored) override {
    std::ignore = ignored;
    // Overwrite the select method to do nothing.
  }
};
}  // namespace

Element unselectable(Element child) {
  return std::make_shared<Unselectable>(std::move(child));
}

/// @brief Create an empty selection.
Selection::Selection() = default;

/// @brief Create a selection.
/// @param start_x The x coordinate of the start of the selection.
/// @param start_y The y coordinate of the start of the selection.
/// @param end_x The x coordinate of the end of the selection.
/// @param end_y The y coordinate of the end of the selection.
Selection::Selection(int start_x, int start_y, int end_x, int end_y)
    : Selection(start_x,
                start_y,
                end_x,
                end_y,
                start_x,
                start_y,
                end_x,
                end_y) {}

/// @brief Create a selection whose endpoints may lie on scrolled content.
///
/// Every node selects with the anchors. A node laying out content scrolled
/// beyond its own box selects that content with the content anchors instead,
/// which may lie outside of the screen. See ScrolledContent().
/// @param start_x The x coordinate of the start of the selection.
/// @param start_y The y coordinate of the start of the selection.
/// @param end_x The x coordinate of the end of the selection.
/// @param end_y The y coordinate of the end of the selection.
/// @param content_start_x The x coordinate of the start on scrolled content.
/// @param content_start_y The y coordinate of the start on scrolled content.
/// @param content_end_x The x coordinate of the end on scrolled content.
/// @param content_end_y The y coordinate of the end on scrolled content.
Selection::Selection(int start_x,
                     int start_y,
                     int end_x,
                     int end_y,
                     int content_start_x,
                     int content_start_y,
                     int content_end_x,
                     int content_end_y)
    : start_x_(start_x),
      start_y_(start_y),
      end_x_(end_x),
      end_y_(end_y),
      content_start_x_(content_start_x),
      content_start_y_(content_start_y),
      content_end_x_(content_end_x),
      content_end_y_(content_end_y),
      box_{
          std::min(start_x, end_x),
          std::max(start_x, end_x),
          std::min(start_y, end_y),
          std::max(start_y, end_y),
      },
      empty_(false) {}

Selection::Selection(int start_x,
                     int start_y,
                     int end_x,
                     int end_y,
                     Selection* parent)
    : start_x_(start_x),
      start_y_(start_y),
      end_x_(end_x),
      end_y_(end_y),
      content_start_x_(start_x),
      content_start_y_(start_y),
      content_end_x_(end_x),
      content_end_y_(end_y),
      box_{
          std::min(start_x, end_x),
          std::max(start_x, end_x),
          std::min(start_y, end_y),
          std::max(start_y, end_y),
      },
      parent_(parent),
      empty_(false) {}

/// @brief Get the box of the selection.
/// @return The box of the selection.
const Box& Selection::GetBox() const {
  return box_;
}

/// @brief Saturate the selection to be inside the box.
/// This is called by `hbox` to propagate the selection to its children.
/// @param box The box to saturate the selection in.
/// @return The saturated selection.
Selection Selection::SaturateHorizontal(Box box) {
  int start_x = start_x_;
  int start_y = start_y_;
  int end_x = end_x_;
  int end_y = end_y_;

  const bool start_outside = !box.Contain(start_x, start_y);
  const bool end_outside = !box.Contain(end_x, end_y);
  const bool properly_ordered =
      start_y < end_y || (start_y == end_y && start_x <= end_x);
  if (properly_ordered) {
    if (start_outside) {
      start_x = box.x_min;
      start_y = box.y_min;
    }
    if (end_outside) {
      end_x = box.x_max;
      end_y = box.y_max;
    }
  } else {
    if (start_outside) {
      start_x = box.x_max;
      start_y = box.y_max;
    }
    if (end_outside) {
      end_x = box.x_min;
      end_y = box.y_min;
    }
  }
  return {
      start_x, start_y, end_x, end_y, parent_,
  };
}

/// @brief Saturate the selection to be inside the box.
/// This is called by `vbox` to propagate the selection to its children.
/// @param box The box to saturate the selection in.
/// @return The saturated selection.
Selection Selection::SaturateVertical(Box box) {
  int start_x = start_x_;
  int start_y = start_y_;
  int end_x = end_x_;
  int end_y = end_y_;

  const bool start_outside = !box.Contain(start_x, start_y);
  const bool end_outside = !box.Contain(end_x, end_y);
  const bool properly_ordered =
      start_y < end_y || (start_y == end_y && start_x <= end_x);

  if (properly_ordered) {
    if (start_outside) {
      start_x = box.x_min;
      start_y = box.y_min;
    }
    if (end_outside) {
      end_x = box.x_max;
      end_y = box.y_max;
    }
  } else {
    if (start_outside) {
      start_x = box.x_max;
      start_y = box.y_max;
    }
    if (end_outside) {
      end_x = box.x_min;
      end_y = box.y_min;
    }
  }
  return {start_x, start_y, end_x, end_y, parent_};
}

/// @brief The selection to propagate into content scrolled within a node.
///
/// Ancestors saturate a selection to their own box, which never exceeds the
/// screen, so a node laying out its content beyond its box (scrolled out of
/// view) could not select the part of it that lies outside. That node selects
/// its content with this selection instead: it spans the content anchors of
/// the root selection, unaffected by any saturation, and still reports its
/// parts to the root selection.
/// @return The selection between the content anchors.
Selection Selection::ScrolledContent() const {
  return {
      parent_->content_start_x_, parent_->content_start_y_,
      parent_->content_end_x_,   parent_->content_end_y_,
      parent_,
  };
}

void Selection::AddPart(std::string_view part, int y, int left, int right) {
  if (parent_ != this) {
    parent_->AddPart(part, y, left, right);
    return;
  }
  [&] {
    if (parts_.str().empty()) {
      parts_ << part;
      return;
    }

    if (y_ != y) {
      parts_ << '\n' << part;
      return;
    }

    if (x_ == left + 1) {
      parts_ << part;
      return;
    }

    parts_ << part;
  }();
  y_ = y;
  x_ = right;
}

}  // namespace ftxui
