// SPDX-License-Identifier: GPL-3.0-or-later
// The depth brush: in the editor's depth mode, strokes correct the
// wallpaper's depth field by hand (Front, Back, Match, Erase), with a smart
// brush that stays on one side of the image's edges. Corrections are kept per
// wallpaper and apply to every widget, the plugin-plane ones included; the
// wallpaper is tinted where it would cover the selected widget.
#include "app.hpp"

#include <cmath>

namespace undershell {

float App::previewPlane() const {
  if (m_selected && m_selected->cfg.depthLevel > 0) return static_cast<float>(m_selected->cfg.depthLevel / 100.0);
  return static_cast<float>(m_noctalia.state().depthThreshold);
}

// output point -> field pixel, and the brush radius in field pixels
bool App::paintMap(const Output* o, double x, double y, double& fx, double& fy, double& fr) {
  if (!o) return false;
  DepthMask* m = m_depth.paintable(o->name);
  if (!m || m->imageW <= 0) return false;
  const double ow = o->logicalW(), oh = o->logicalH();
  const int fill = m_noctalia.state().fillMode;
  double u = 0, v = 0, u2 = 0, v2 = 0;
  if (!wallpaperUv(x, y, ow, oh, m->imageW, m->imageH, fill, u, v)) return false;
  wallpaperUv(x + m_brush, y, ow, oh, m->imageW, m->imageH, fill, u2, v2);
  fx = u * m->fieldW;
  fy = v * m->fieldH;
  fr = std::max(1.0, std::abs(u2 - u) * m->fieldW);
  return true;
}

void App::paintDab(double x, double y) {
  const Output* o = m_pointerEdit ? m_pointerEdit->output : nullptr;
  DepthMask* m = o ? m_depth.paintable(o->name) : nullptr;
  double fx = 0, fy = 0, fr = 0;
  if (!m || !paintMap(o, x, y, fx, fy, fr)) return;
  PixelBox dab;
  const bool smart = m_smartBrush && m_paintTool != static_cast<int>(DepthTool::Erase);
  // the smart brush looks at the image's tone only: the depth is what is
  // being fixed, so it must not decide where the stroke may go
  stampDab(m_stroke, m->fieldW, m->fieldH, fx, fy, fr, dab, smart ? &m->guide : nullptr);
  if (dab.empty()) return;
  m_strokeBox.add(dab.x0, dab.y0, 0);
  m_strokeBox.add(dab.x1 - 1, dab.y1 - 1, 0);
  // live: this dab's box, the stroke previewed over the saved corrections
  m_depth.uploadRect(*m, dab, &m_stroke, static_cast<DepthTool>(m_paintTool), m_matchValue);
  for (auto& w : m_widgets) w->needsRender = true;
}

void App::paintBegin(double x, double y) {
  const Output* o = m_pointerEdit ? m_pointerEdit->output : nullptr;
  DepthMask* m = o ? m_depth.paintable(o->name) : nullptr;
  double fx = 0, fy = 0, fr = 0;
  if (!m || !paintMap(o, x, y, fx, fy, fr)) return;
  m_painting = true;
  m_paintOutput = o->name;
  m_stroke.assign(static_cast<size_t>(m->fieldW) * m->fieldH, 0.0F);
  m_strokeBox = {};
  // Match takes the depth where the stroke starts
  const size_t i = static_cast<size_t>(std::clamp(static_cast<int>(fy), 0, m->fieldH - 1)) * m->fieldW +
                   static_cast<size_t>(std::clamp(static_cast<int>(fx), 0, m->fieldW - 1));
  m_matchValue = editedDepth(m->base.v[i], m->edits.target[i], m->edits.cover[i]);
  m_lastDabX = x;
  m_lastDabY = y;
  paintDab(x, y);
}

void App::paintMove(double x, double y) {
  if (!m_painting) return;
  // dabs spaced along the path so fast strokes stay continuous
  const double step = std::max(2.0, m_brush * 0.3);
  const double dx = x - m_lastDabX, dy = y - m_lastDabY, len = std::hypot(dx, dy);
  if (len < step) return;
  const int n = static_cast<int>(len / step);
  for (int k = 1; k <= n; ++k) paintDab(m_lastDabX + dx * k / n, m_lastDabY + dy * k / n);
  m_lastDabX = x;
  m_lastDabY = y;
}

void App::paintEnd() {
  if (!m_painting) return;
  m_painting = false;
  DepthMask* m = m_depth.paintable(m_paintOutput);
  if (!m || m_strokeBox.empty()) return;
  m_editsUndo.push_back({m_paintOutput, m->edits});
  if (m_editsUndo.size() > 12) m_editsUndo.erase(m_editsUndo.begin());
  mergeStroke(m->edits, m_stroke, static_cast<DepthTool>(m_paintTool), m_matchValue, m_strokeBox);
  m_depth.uploadRect(*m, m_strokeBox);
  m_depth.saveEdits(*m);
  m_stroke.clear();
  m_stroke.shrink_to_fit();
  for (auto& w : m_widgets) w->needsRender = true;
  markEditDirty();
}

void App::paintUndo() {
  if (m_editsUndo.empty()) return;
  auto [out, edits] = m_editsUndo.back();
  m_editsUndo.pop_back();
  DepthMask* m = m_depth.paintable(out);
  if (!m || edits.w != m->fieldW || edits.h != m->fieldH) return;
  m->edits = std::move(edits);
  m_depth.uploadRect(*m, {0, 0, m->fieldW, m->fieldH});
  m_depth.saveEdits(*m);
  for (auto& w : m_widgets) w->needsRender = true;
  markEditDirty();
}

void App::paintClear() {
  const Output* o = m_uiOutput;
  DepthMask* m = o ? m_depth.paintable(o->name) : nullptr;
  if (!m || m->edits.empty()) return;
  m_editsUndo.push_back({o->name, m->edits});
  m->edits.reset(m->fieldW, m->fieldH);
  m_depth.uploadRect(*m, {0, 0, m->fieldW, m->fieldH});
  m_depth.saveEdits(*m);
  for (auto& w : m_widgets) w->needsRender = true;
  markEditDirty();
}

void App::setPaintMode(bool on) {
  if (on == m_paintMode) return;
  paintEnd();
  m_paintMode = on;
  m_confirmClear = false;
  m_galleryOpen = m_helpOpen = m_savesOpen = false;
  m_drag = Drag::None;
  m_guidesV.clear();
  m_guidesH.clear();
  markEditDirty();
}

}  // namespace undershell
