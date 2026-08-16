// Copyright 2020 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#include <memory>   // for make_shared
#include <utility>  // for move

#include "ftxui/dom/elements.hpp"        // for Element, clear_under
#include "ftxui/dom/node.hpp"            // for Node
#include "ftxui/dom/node_decorator.hpp"  // for NodeDecorator
#include "ftxui/screen/box.hpp"          // for Box
#include "ftxui/screen/screen.hpp"       // for Cell, Screen

namespace ftxui {

namespace {
using ftxui::Screen;

class ClearUnder : public NodeDecorator {
 public:
  using NodeDecorator::NodeDecorator;

  void Render(Screen& screen) override {
    const Box clipped = Box::Intersection(box_, screen.stencil);
    for (int y = clipped.y_min; y <= clipped.y_max; ++y) {
      for (int x = clipped.x_min; x <= clipped.x_max; ++x) {
        screen.CellAt(x, y) = Cell();
        screen.CellAt(x, y).character = " ";  // Consider the cell written.
      }
    }
    Node::Render(screen);
  }
};
}  // namespace

/// @brief Before drawing |child|, clear the cells below. This is useful in
///        combination with dbox.
/// @see ftxui::dbox
/// @ingroup dom
Element clear_under(Element element) {
  return std::make_shared<ClearUnder>(std::move(element));
}

}  // namespace ftxui
