// SPDX-License-Identifier: GPL-3.0-or-later
// A note on the desktop, kept in a Markdown file: headings, lists and tasks
// ("- [ ]" / "- [x]", ticked with a click on the desktop). It is written in
// the editor straight on the widget (double-click it), or in any text editor:
// the file is watched, so a change shows at once and nothing is polled. On
// a sticky-note paper, a glass plate or nothing. Drawn once per change.
#pragma once

#include "canvas.hpp"
#include "widget.hpp"

#include <string>
#include <vector>

namespace undershell {

struct NotesConfig {
  std::string file;                 // the .md; empty: the data folder's notes/<id>.md
  std::string plate = "paper";      // paper glass none
  std::string paper = "#ffe58a";    // the sticky note's colour
  std::string ink = "auto";         // auto: dark on paper, the theme's text otherwise
  std::string accent = "primary";   // headings' marks, ticked boxes
  std::string font = "Space Grotesk";
  double size = 18;                 // text size, px
  double opacity = 1;

  static NotesConfig fromTable(const toml::table& t);
};

class NotesWidget final : public WidgetImpl {
public:
  void configure(const WidgetConfig& cfg, const NoctaliaState& noct) override;
  void draw(const DrawContext& ctx) override;
  [[nodiscard]] Color accent() const override { return m_accentC; }
  [[nodiscard]] std::vector<Rect> inputRects() const override;
  bool onPointer(const PointerEvent& ev) override;
  [[nodiscard]] const char* cursor() const override { return m_hover >= 0 ? "pointer" : "default"; }
  [[nodiscard]] std::vector<std::string> watchedFiles() const override { return {m_path}; }
  void filesChanged() override { load(); }
  [[nodiscard]] std::string editableText() const override { return m_text; }
  void setEditing(const std::string* text, size_t caret) override;
  void saveEditedText(const std::string& text) override;

  // the file a note with this id and config is kept in
  static std::string pathFor(const std::string& id, const std::string& file);

  struct Line {
    enum Kind { Text, Heading1, Heading2, Heading3, Bullet, Task, Done, Quote, Rule, Blank } kind = Text;
    std::string text;  // without its marker
    int indent = 0;    // list nesting (two spaces each)
  };
  static std::vector<Line> parse(const std::string& markdown);

private:
  void load();
  void toggleTask(int line);

  NotesConfig m_cfg;
  std::string m_path, m_text;
  Color m_inkC, m_accentC, m_paperC, m_surfaceC;
  bool m_editing = false;
  std::string m_draft;
  size_t m_caret = 0;
  Canvas m_canvas;
  struct Box {
    Rect r;
    int line = 0;  // the source line of the task
  };
  std::vector<Box> m_boxes;  // the tick boxes as last drawn
  int m_hover = -1;
};

}  // namespace undershell
