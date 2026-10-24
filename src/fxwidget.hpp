// SPDX-License-Identifier: GPL-3.0-or-later
// Animated effects on any widget (a clock in the wind, a picture under
// water, text with light rays): createWidget() wraps every type but the
// wallpaper one in this. With no effect on it only passes calls through;
// with some, the widget draws into a texture of its own size and the
// effects draw that texture onto the surface (one pass, plus the small
// lights pass). Effects here work in the widget's own box: "where" is
// everywhere or above / below a line (the wallpaper's depth and painted
// masks belong to the wallpaper).
#pragma once

#include "effectchain.hpp"
#include "widget.hpp"

#include <memory>

namespace undershell {

class FxWidget final : public WidgetImpl {
public:
  explicit FxWidget(std::unique_ptr<WidgetImpl> inner) : m_inner(std::move(inner)) {}
  ~FxWidget() override;

  void configure(const WidgetConfig& cfg, const NoctaliaState& noct) override;
  void tick(const TickContext& ctx) override;
  void draw(const DrawContext& ctx) override;

  [[nodiscard]] bool animating(const TickContext& ctx) const override { return m_inner->animating(ctx) || m_fx.moving(); }
  [[nodiscard]] bool visible() const override { return m_inner->visible(); }
  [[nodiscard]] double nextWakeup(double now) const override { return m_inner->nextWakeup(now); }
  [[nodiscard]] int fps() const override { return m_fx.empty() ? m_inner->fps() : std::max(m_inner->fps(), m_fx.fps()); }
  [[nodiscard]] bool fullscreen() const override { return m_inner->fullscreen(); }
  [[nodiscard]] bool usesAudio() const override { return m_inner->usesAudio() || m_fx.audio(); }
  [[nodiscard]] bool wantsDepth() const override { return m_inner->wantsDepth(); }
  [[nodiscard]] std::string stillWallpaper() const override { return m_inner->stillWallpaper(); }
  [[nodiscard]] bool keepsResolution() const override { return m_inner->keepsResolution(); }
  [[nodiscard]] Color accent() const override { return m_inner->accent(); }
  void rest() override { m_inner->rest(); }
  void setHidden(bool hidden) override { m_inner->setHidden(hidden); }
  [[nodiscard]] std::vector<Rect> inputRects() const override { return m_inner->inputRects(); }
  bool onPointer(const PointerEvent& ev) override { return m_inner->onPointer(ev); }
  [[nodiscard]] const char* cursor() const override { return m_inner->cursor(); }
  [[nodiscard]] std::vector<std::string> watchedFiles() const override;
  void filesChanged() override;
  [[nodiscard]] std::string editableText() const override { return m_inner->editableText(); }
  void setEditing(const std::string* text, size_t caret) override { m_inner->setEditing(text, caret); }
  void saveEditedText(const std::string& text) override { m_inner->saveEditedText(text); }
  [[nodiscard]] bool wantsMedia() const override { return m_inner->wantsMedia(); }
  [[nodiscard]] bool wantsLyrics() const override { return m_inner->wantsLyrics(); }
  [[nodiscard]] bool wantsWeather() const override { return m_inner->wantsWeather(); }

private:
  std::unique_ptr<WidgetImpl> m_inner;
  EffectChain m_fx;
  GLuint m_fbo = 0, m_tex = 0;
  int m_w = 0, m_h = 0;
};

}  // namespace undershell
