// SPDX-License-Identifier: GPL-3.0-or-later
#include "widget.hpp"

#include "calendar.hpp"
#include "clock.hpp"
#include "filter.hpp"
#include "fxwidget.hpp"
#include "notes.hpp"
#include "nowplaying.hpp"
#include "image.hpp"
#include "lyricswidget.hpp"
#include "shape.hpp"
#include "textwidget.hpp"
#include "visualizer.hpp"
#include "weather.hpp"

namespace undershell {

namespace {
std::unique_ptr<WidgetImpl> plainWidget(const std::string& type) {
  if (type == "visualizer") return std::make_unique<Visualizer>();
  if (type == "clock") return std::make_unique<ClockWidget>();
  if (type == "now_playing") return std::make_unique<NowPlayingWidget>();
  if (type == "shape") return std::make_unique<ShapeWidget>();
  if (type == "text") return std::make_unique<TextWidget>();
  if (type == "lyrics") return std::make_unique<LyricsWidget>();
  if (type == "calendar") return std::make_unique<CalendarWidget>();
  if (type == "notes") return std::make_unique<NotesWidget>();
  if (type == "weather") return std::make_unique<WeatherWidget>();
  if (type == "image") return std::make_unique<ImageWidget>();
  return nullptr;
}
}  // namespace

std::unique_ptr<WidgetImpl> createWidget(const std::string& type) {
  if (type == "filter") return std::make_unique<FilterWidget>();  // the wallpaper's effects are its own
  // every other type can carry animated effects (fxwidget.hpp)
  if (auto w = plainWidget(type)) return std::make_unique<FxWidget>(std::move(w));
  return nullptr;
}

std::vector<std::string> widgetTypes() { return {"visualizer", "clock", "now_playing", "shape", "image", "text", "lyrics", "calendar", "notes", "weather", "filter"}; }

}  // namespace undershell
