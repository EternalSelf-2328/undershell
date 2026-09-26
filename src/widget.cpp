// SPDX-License-Identifier: GPL-3.0-or-later
#include "widget.hpp"

#include "visualizer.hpp"

namespace undershell {

std::unique_ptr<WidgetImpl> createWidget(const std::string& type) {
  if (type == "visualizer") return std::make_unique<Visualizer>();
  return nullptr;
}

std::vector<std::string> widgetTypes() { return {"visualizer"}; }

}  // namespace undershell
