// Copyright 2020 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#include <memory>   // for make_shared
#include <utility>  // for move

#include "ftxui/dom/elements.hpp"        // for Element, underlined
#include "ftxui/dom/node.hpp"            // for Node
#include "ftxui/dom/node_decorator.hpp"  // for NodeDecorator
#include "ftxui/screen/box.hpp"          // for Box
#include "ftxui/screen/screen.hpp"       // for Cell, Screen

namespace ftxui {

namespace {
class Underlined : public NodeDecorator {
 public:
  using NodeDecorator::NodeDecorator;

  void Render(Screen& screen) override {
    Node::Render(screen);
    const Box clipped = Box::Intersection(box_, screen.stencil);
      for (int y = clipped.y_min; y <= clipped.y_max; ++y) {
      for (int x = clipped.x_min; x <= clipped.x_max; ++x) {
        screen.CellAt(x, y).underlined = true;
      }
    }
  }
};
}  // namespace

/// @brief Underline the given element.
/// @ingroup dom
Element underlined(Element child) {
  return std::make_shared<Underlined>(std::move(child));
}

}  // namespace ftxui
