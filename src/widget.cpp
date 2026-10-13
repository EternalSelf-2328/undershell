// SPDX-License-Identifier: GPL-3.0-or-later
#include "widget.hpp"

#include "calendar.hpp"
#include "clock.hpp"
#include "notes.hpp"
#include "nowplaying.hpp"
#include "image.hpp"
#include "lyricswidget.hpp"
#include "shape.hpp"
#include "textwidget.hpp"
#include "visualizer.hpp"

namespace undershell {

std::unique_ptr<WidgetImpl> createWidget(const std::string& type) {
  if (type == "visualizer") return std::make_unique<Visualizer>();
  if (type == "clock") return std::make_unique<ClockWidget>();
  if (type == "now_playing") return std::make_unique<NowPlayingWidget>();
  if (type == "shape") return std::make_unique<ShapeWidget>();
  if (type == "text") return std::make_unique<TextWidget>();
  if (type == "lyrics") return std::make_unique<LyricsWidget>();
  if (type == "calendar") return std::make_unique<CalendarWidget>();
  if (type == "notes") return std::make_unique<NotesWidget>();
  if (type == "image") return std::make_unique<ImageWidget>();
  return nullptr;
}

std::vector<std::string> widgetTypes() { return {"visualizer", "clock", "now_playing", "shape", "image", "text", "lyrics", "calendar", "notes"}; }

}  // namespace undershell
