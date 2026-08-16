// Copyright 2020 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#include <memory>   // for make_shared
#include <utility>  // for move

#include "ftxui/dom/elements.hpp"        // for Element, bold
#include "ftxui/dom/node.hpp"            // for Node
#include "ftxui/dom/node_decorator.hpp"  // for NodeDecorator
#include "ftxui/screen/box.hpp"          // for Box
#include "ftxui/screen/screen.hpp"       // for Cell, Screen

namespace ftxui {

namespace {
class Bold : public NodeDecorator {
 public:
  using NodeDecorator::NodeDecorator;

  void Render(Screen& screen) override {
    const Box clipped = Box::Intersection(box_, screen.stencil);
      for (int y = clipped.y_min; y <= clipped.y_max; ++y) {
      for (int x = clipped.x_min; x <= clipped.x_max; ++x) {
        screen.CellAt(x, y).bold = true;
      }
    }
    Node::Render(screen);
  }
};
}  // namespace

/// @brief Use a bold font, for elements with more emphasis.
/// @ingroup dom
Element bold(Element child) {
  return std::make_shared<Bold>(std::move(child));
}

}  // namespace ftxui
