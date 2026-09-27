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
  wallpaperUv(x + m_brush / m_zoom, y, ow, oh, m->imageW, m->imageH, fill, u2, v2);  // finer when zoomed
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
  const bool smooth = m_paintTool == static_cast<int>(DepthTool::Smooth);
  if (smooth) blurredDepth(m->base, m->edits, dab, 12, m_smoothTarget);
  // live: this dab's box, the stroke previewed over the saved corrections
  m_depth.uploadRect(*m, dab, &m_stroke, static_cast<DepthTool>(m_paintTool), m_matchValue, smooth ? &m_smoothTarget : nullptr);
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
  m_smoothTarget.assign(m_stroke.size(), 0.0F);
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
  const double step = std::max(0.5, m_brush / m_zoom * 0.3);  // world units: finer when zoomed
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
  const bool smooth = m_paintTool == static_cast<int>(DepthTool::Smooth);
  if (smooth) blurredDepth(m->base, m->edits, m_strokeBox, 12, m_smoothTarget);
  mergeStroke(m->edits, m_stroke, static_cast<DepthTool>(m_paintTool), m_matchValue, m_strokeBox, smooth ? &m_smoothTarget : nullptr);
  m_depth.uploadRect(*m, m_strokeBox);
  m_depth.saveEdits(*m);
  m_stroke.clear();
  m_stroke.shrink_to_fit();
  m_smoothTarget.clear();
  m_smoothTarget.shrink_to_fit();
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
  m_lasso.clear();
  m_panning = false;
  m_zoom = 1;
  m_confirmClear = false;
  m_galleryOpen = m_helpOpen = m_savesOpen = false;
  m_drag = Drag::None;
  m_guidesV.clear();
  m_guidesH.clear();
  markEditDirty();
}

// ── the zoomed view ─────────────────────────────────────────────────────────
// Screen points map to "world" (output) points through the brush view:
// world = view + (screen - centre) / zoom. At zoom 1 they are the same.

void App::paintWorld(const Output* o, double sx, double sy, double& wx, double& wy) const {
  if (!o || m_zoom <= 1.0001) {
    wx = sx;
    wy = sy;
    return;
  }
  wx = m_viewX + (sx - o->logicalW() / 2) / m_zoom;
  wy = m_viewY + (sy - o->logicalH() / 2) / m_zoom;
}

void App::paintScreen(const Output* o, double wx, double wy, double& sx, double& sy) const {
  if (!o || m_zoom <= 1.0001) {
    sx = wx;
    sy = wy;
    return;
  }
  sx = o->logicalW() / 2 + (wx - m_viewX) * m_zoom;
  sy = o->logicalH() / 2 + (wy - m_viewY) * m_zoom;
}

void App::clampPaintView(const Output* o) {
  if (!o) return;
  const double hw = o->logicalW() / 2, hh = o->logicalH() / 2;
  m_viewX = std::clamp(m_viewX, hw / m_zoom, o->logicalW() - hw / m_zoom);
  m_viewY = std::clamp(m_viewY, hh / m_zoom, o->logicalH() - hh / m_zoom);
}

void App::paintZoom(double factor, double sx, double sy) {
  const Output* o = m_pointerEdit ? m_pointerEdit->output : m_uiOutput;
  if (!o) return;
  if (m_zoom <= 1.0001) {
    m_viewX = o->logicalW() / 2;
    m_viewY = o->logicalH() / 2;
  }
  double wx = 0, wy = 0;
  paintWorld(o, sx, sy, wx, wy);  // keep this point under the pointer
  m_zoom = std::clamp(m_zoom * factor, 1.0, 8.0);
  m_viewX = wx - (sx - o->logicalW() / 2) / m_zoom;
  m_viewY = wy - (sy - o->logicalH() / 2) / m_zoom;
  clampPaintView(o);
  markEditDirty();
}

// ── selections: the wand and the lasso ──────────────────────────────────────

bool App::fieldAt(const Output* o, double wx, double wy, int& fx, int& fy) {
  double x = 0, y = 0, r = 0;
  if (!paintMap(o, wx, wy, x, y, r)) return false;
  fx = static_cast<int>(x);
  fy = static_cast<int>(y);
  return true;
}

// what the current action does to a whole selection, as one undo step
void App::applySelection(DepthMask& m, std::vector<float>& mask, PixelBox box, float matchValue) {
  if (box.empty()) return;
  m_editsUndo.push_back({m_paintOutput, m.edits});
  if (m_editsUndo.size() > 12) m_editsUndo.erase(m_editsUndo.begin());
  std::vector<float> target;
  const bool smooth = m_paintTool == static_cast<int>(DepthTool::Smooth);
  if (smooth) blurredDepth(m.base, m.edits, box, 12, target);
  mergeStroke(m.edits, mask, static_cast<DepthTool>(m_paintTool), matchValue, box, smooth ? &target : nullptr);
  m_depth.uploadRect(m, box);
  m_depth.saveEdits(m);
  for (auto& w : m_widgets) w->needsRender = true;
  markEditDirty();
}

void App::wandAt(double wx, double wy) {
  const Output* o = m_pointerEdit ? m_pointerEdit->output : nullptr;
  DepthMask* m = o ? m_depth.paintable(o->name) : nullptr;
  int fx = 0, fy = 0;
  if (!m || !fieldAt(o, wx, wy, fx, fy)) return;
  m_paintOutput = o->name;
  PixelBox box;
  // the wand looks at a coarser tone than the brush, so hatching and ink
  // lines inside an object read as one surface
  const auto coarse = boxMean(m->guide, m->fieldW, m->fieldH, 6);
  auto mask = regionGrow(coarse, m->base.v, m->fieldW, m->fieldH, fx, fy, m_wandTolerance, box);
  const size_t i = static_cast<size_t>(fy) * m->fieldW + fx;
  applySelection(*m, mask, box, editedDepth(m->base.v[i], m->edits.target[i], m->edits.cover[i]));
}

void App::lassoClick(double sx, double sy) {
  const Output* o = m_pointerEdit ? m_pointerEdit->output : nullptr;
  if (!o) return;
  const double now = nowSeconds();
  if (m_lasso.size() >= 3) {
    // closing: back on the first point, or a double click
    double fx = 0, fy = 0;
    paintScreen(o, m_lasso.front().first, m_lasso.front().second, fx, fy);
    if (std::hypot(sx - fx, sy - fy) < 14 || now - m_lastClickAt < 0.35) {
      m_lastClickAt = 0;
      lassoClose();
      return;
    }
  }
  m_lastClickAt = now;
  double wx = 0, wy = 0;
  paintWorld(o, sx, sy, wx, wy);
  m_lasso.emplace_back(wx, wy);
  markEditDirty();
}

void App::lassoClose() {
  const Output* o = m_pointerEdit ? m_pointerEdit->output : m_uiOutput;
  DepthMask* m = o ? m_depth.paintable(o->name) : nullptr;
  if (!m || m_lasso.size() < 3) {
    m_lasso.clear();
    markEditDirty();
    return;
  }
  m_paintOutput = o->name;
  std::vector<std::pair<double, double>> pts;
  for (const auto& [wx, wy] : m_lasso) {
    double x = 0, y = 0, r = 0;
    if (paintMap(o, wx, wy, x, y, r)) pts.emplace_back(x, y);
  }
  int fx = 0, fy = 0;
  const bool haveMatch = fieldAt(o, m_lasso.front().first, m_lasso.front().second, fx, fy);
  m_lasso.clear();
  if (pts.size() < 3) return;
  std::vector<float> mask(static_cast<size_t>(m->fieldW) * m->fieldH, 0.0F);
  PixelBox box;
  fillPolygon(mask, m->fieldW, m->fieldH, pts, box);
  if (m_smartBrush) snapMask(m->guide, mask, m->fieldW, m->fieldH, box, 10);  // onto the nearby edges
  box.add(box.x0, box.y0, 22);
  box.add(box.x1 - 1, box.y1 - 1, 22);
  box.clip(m->fieldW, m->fieldH);
  const size_t i = haveMatch ? static_cast<size_t>(fy) * m->fieldW + fx : 0;
  applySelection(*m, mask, box, haveMatch ? editedDepth(m->base.v[i], m->edits.target[i], m->edits.cover[i]) : 0.5F);
}

}  // namespace undershell
