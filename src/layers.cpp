// SPDX-License-Identifier: GPL-3.0-or-later
// The editor's layers: the widgets of a monitor front to back as the Layers
// panel lists them (a group's members under its name), the selection of
// several widgets, stacking them in another order, groups (widgets picked,
// moved, scaled and turned together), hiding and locking.
//
// A group is the `group` key its widgets share, and the stacking order is
// their `layer` numbers, so a layout keeps both wherever its blocks go
// (profiles, saved layouts). Changes to several widgets rewrite their blocks
// together: one undo step.
#include "app.hpp"
#include "i18n.hpp"
#include "snap.hpp"

#include <cmath>
#include <format>
#include <linux/input-event-codes.h>
#include <numbers>
#include <regex>
#include <xkbcommon/xkbcommon.h>

namespace undershell {

namespace {
constexpr double kDoubleClick = 0.35;

std::string tomlString(const std::string& s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    if (static_cast<unsigned char>(c) >= 0x20) o += c;
  }
  return o + "\"";
}

std::string degrees(double d) {
  const double r = std::round(d * 10) / 10;
  return r == std::floor(r) ? std::format("{:.0f}", r) : std::format("{:.1f}", r);
}

// a point turned by `a` radians about (cx, cy)
void turn(double& x, double& y, double cx, double cy, double a) {
  const double dx = x - cx, dy = y - cy;
  x = cx + dx * std::cos(a) - dy * std::sin(a);
  y = cy + dx * std::sin(a) + dy * std::cos(a);
}
}  // namespace

// ── the stack ───────────────────────────────────────────────────────────────

std::vector<Widget*> App::stackOf(const Output* o) const {
  std::vector<Widget*> out;
  for (const auto& w : m_widgets)  // restack() keeps them bottom to top
    if (w->impl && w->output == o) out.push_back(w.get());
  return out;
}

std::vector<App::LayerItem> App::layerItems(const Output* o) const {
  const auto stack = stackOf(o);
  std::vector<LayerItem> out;
  std::set<std::string> listed;
  for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
    Widget* w = *it;
    if (w->cfg.group.empty()) {
      out.push_back({w, {}, false});
      continue;
    }
    if (listed.count(w->cfg.group)) continue;
    // the group shows where its front member is, all its members under it
    listed.insert(w->cfg.group);
    out.push_back({nullptr, w->cfg.group, false});
    if (m_foldedGroups.count(w->cfg.group)) continue;
    for (auto jt = it; jt != stack.rend(); ++jt)
      if ((*jt)->cfg.group == w->cfg.group) out.push_back({*jt, w->cfg.group, true});
  }
  return out;
}

std::vector<Widget*> App::groupMembers(const Output* o, const std::string& group) const {
  std::vector<Widget*> out;
  if (group.empty()) return out;
  for (Widget* w : stackOf(o))
    if (w->cfg.group == group) out.push_back(w);
  return out;
}

// ── selection ───────────────────────────────────────────────────────────────

std::vector<Widget*> App::selection() {
  std::vector<Widget*> out;
  if (!m_selected) return out;
  out.push_back(m_selected);
  // the others belong to this selection only (anything that sets m_selected alone drops them)
  if (m_multiFor != m_selected->cfg.id) {
    m_multi.clear();
    return out;
  }
  for (const auto& id : m_multi)
    for (auto& w : m_widgets)
      if (w->cfg.id == id && w.get() != m_selected && w->output == m_selected->output && w->impl) out.push_back(w.get());
  return out;
}

bool App::isSelected(const Widget* w) {
  if (!w) return false;
  for (Widget* s : selection())
    if (s == w) return true;
  return false;
}

void App::selectOnly(Widget* w) {
  if (w != m_selected) {
    m_inspScroll = 0;
    m_pinCorner = -1;
    m_fontPickFor.clear();
    m_elExpanded.clear();
    closeColorPick();
  }
  m_selected = w;
  m_multi.clear();
  m_multiFor.clear();
  markEditDirty();
}

void App::selectGroup(Widget* w) {
  selectOnly(w);
  if (!w || w->cfg.group.empty()) return;
  for (Widget* m : groupMembers(w->output, w->cfg.group))
    if (m != w && !m->cfg.hidden) m_multi.push_back(m->cfg.id);
  m_multiFor = w->cfg.id;
}

void App::toggleSelected(Widget* w) {
  if (!w) return;
  auto sel = selection();
  if (!m_selected || w->output != m_selected->output) {
    selectGroup(w);
    return;
  }
  // a group comes and goes whole
  std::vector<Widget*> unit = w->cfg.group.empty() ? std::vector<Widget*>{w} : groupMembers(w->output, w->cfg.group);
  const bool in = std::find(sel.begin(), sel.end(), w) != sel.end();
  std::vector<std::string> ids;
  for (Widget* s : sel)
    if (!in || std::find(unit.begin(), unit.end(), s) == unit.end()) ids.push_back(s->cfg.id);
  if (!in)
    for (Widget* u : unit)
      if (!u->cfg.hidden && std::find(sel.begin(), sel.end(), u) == sel.end()) ids.push_back(u->cfg.id);
  if (!in) {  // the one clicked leads (its inspector, its knob)
    std::erase(ids, w->cfg.id);
    ids.insert(ids.begin(), w->cfg.id);
  }
  selectIds(ids);
}

void App::selectIds(const std::vector<std::string>& ids) {
  Widget* first = nullptr;
  for (const auto& id : ids)
    for (auto& w : m_widgets)
      if (!first && w->cfg.id == id) first = w.get();
  selectOnly(first);
  if (!first) return;
  for (const auto& id : ids)
    if (id != first->cfg.id) m_multi.push_back(id);
  m_multiFor = first->cfg.id;
}

// ── rewriting several blocks at once ────────────────────────────────────────

void App::editBlocks(const std::function<std::string(const std::string&, const std::string&)>& edit,
                     std::vector<std::string> select) {
  if (select.empty())
    for (Widget* w : selection()) select.push_back(w->cfg.id);
  std::string out;
  bool changed = false;
  for (const auto& b : Config::splitBlocks(Config::widgetBlocks(readFile(m_configPath)))) {
    std::string nb = edit(Config::blockId(b), b);
    changed = changed || nb != b;
    if (nb.empty()) continue;
    if (!out.empty()) out += "\n";
    out += nb;
    if (!out.ends_with("\n")) out += "\n";
  }
  if (!changed) return;
  replaceLayout(out, true);
  selectIds(select);
  markEditDirty();
}

void App::restackAs(const Output* o, const std::vector<Widget*>& bottomToTop, const std::map<std::string, std::string>& groupOf) {
  // the monitor's widgets numbered 1..n bottom to top: the panel's order wins
  // over the depth planes, which only break ties
  std::map<std::string, int> layerOf;
  for (size_t i = 0; i < bottomToTop.size(); ++i) layerOf[bottomToTop[i]->cfg.id] = static_cast<int>(i) + 1;
  std::map<std::string, std::string> before;
  for (Widget* w : stackOf(o)) before[w->cfg.id] = w->cfg.group;
  editBlocks([&](const std::string& id, const std::string& block) {
    std::string b = block;
    if (auto it = layerOf.find(id); it != layerOf.end()) {
      for (Widget* w : bottomToTop)
        if (w->cfg.id == id && w->cfg.layer != it->second) b = Config::setBlockKey(b, "layer", std::to_string(it->second));
    }
    if (auto it = groupOf.find(id); it != groupOf.end() && before[id] != it->second)
      b = Config::setBlockKey(b, "group", it->second.empty() ? std::string() : tomlString(it->second));
    return b;
  });
}

// ── order ───────────────────────────────────────────────────────────────────

void App::raiseSelection(int step) {
  auto sel = selection();
  if (sel.empty()) return;
  const Output* o = sel.front()->output;
  auto stack = stackOf(o);
  std::vector<Widget*> moving, rest;
  for (Widget* w : stack) (std::find(sel.begin(), sel.end(), w) != sel.end() ? moving : rest).push_back(w);
  // inside one group the members change places among themselves; otherwise a
  // whole group is stepped over as one
  std::string inGroup = moving.front()->cfg.group;
  for (Widget* w : moving)
    if (w->cfg.group != inGroup) inGroup.clear();
  const bool wholeGroup = !inGroup.empty() && groupMembers(o, inGroup).size() == moving.size();
  if (wholeGroup) inGroup.clear();
  // where the moving block sits in `rest`: before index `at`
  size_t at = 0;
  for (size_t i = 0; i < stack.size(); ++i)
    if (stack[i] == moving.front()) break;
    else if (std::find(moving.begin(), moving.end(), stack[i]) == moving.end()) ++at;
  auto groupOfAt = [&](size_t i) { return i < rest.size() ? rest[i]->cfg.group : std::string(); };
  size_t lo = 0, hi = rest.size();  // the range it may go to
  if (!inGroup.empty()) {
    lo = rest.size(), hi = 0;
    for (size_t i = 0; i < rest.size(); ++i)
      if (rest[i]->cfg.group == inGroup) lo = std::min(lo, i), hi = std::max(hi, i + 1);
    if (lo > hi) return;  // all of the group is moving
    lo = std::min(lo, at), hi = std::max(hi, at);
  }
  size_t to = at;
  if (step >= 2) to = hi;
  else if (step <= -2) to = lo;
  else if (step > 0 && at < hi) {
    to = at + 1;
    const std::string g = groupOfAt(at);  // over a whole group at once
    if (inGroup.empty() && !g.empty())
      while (to < rest.size() && rest[to]->cfg.group == g) ++to;
  } else if (step < 0 && at > lo) {
    to = at - 1;
    const std::string g = groupOfAt(at - 1);
    if (inGroup.empty() && !g.empty())
      while (to > 0 && rest[to - 1]->cfg.group == g) --to;
  }
  if (to == at) return;
  std::vector<Widget*> next(rest.begin(), rest.begin() + static_cast<long>(to));
  next.insert(next.end(), moving.begin(), moving.end());
  next.insert(next.end(), rest.begin() + static_cast<long>(to), rest.end());
  restackAs(o, next);
}

void App::dropLayer(int slot) {
  const Output* o = m_uiOutput;
  const auto rows = layerItems(o);
  if (m_layerPress.size() < 3) return;
  const bool header = m_layerPress[0] == 'g';
  const std::string key = m_layerPress.substr(2);
  std::vector<Widget*> moving;
  if (header) {
    moving = groupMembers(o, key);
  } else {
    Widget* w = nullptr;
    for (Widget* s : stackOf(o))
      if (s->cfg.id == key) w = s;
    if (!w) return;
    moving = isSelected(w) ? selection() : std::vector<Widget*>{w};
  }
  if (moving.empty()) return;
  slot = std::clamp(slot, 0, static_cast<int>(rows.size()));
  auto moves = [&](const LayerItem& r) {
    if (!r.w) return header && r.group == key;
    return std::find(moving.begin(), moving.end(), r.w) != moving.end();
  };
  // a widget let go between two members of a group joins it; anywhere else
  // it leaves its group. A group never goes inside another.
  std::string target;
  if (slot > 0 && !header) {
    const LayerItem& above = rows[static_cast<size_t>(slot - 1)];
    const bool belowIn = slot < static_cast<int>(rows.size()) && rows[static_cast<size_t>(slot)].child &&
                         rows[static_cast<size_t>(slot)].group == above.group;
    if (!above.group.empty() && (!above.w || (above.child && belowIn))) target = above.group;
  }
  if (header)  // out of any group it was dropped inside
    while (slot > 0 && slot < static_cast<int>(rows.size()) && rows[static_cast<size_t>(slot)].child) --slot;
  // what ends up just under the moved rows: the first row below the slot that stays
  Widget* below = nullptr;
  for (size_t i = static_cast<size_t>(slot); i < rows.size() && !below; ++i) {
    if (moves(rows[i])) continue;
    if (rows[i].w) below = rows[i].w;
    else {
      auto members = groupMembers(o, rows[i].group);  // a folded or open group: its front member
      for (auto it = members.rbegin(); it != members.rend() && !below; ++it)
        if (std::find(moving.begin(), moving.end(), *it) == moving.end()) below = *it;
    }
  }
  std::vector<Widget*> rest;  // front to back
  const auto stack = stackOf(o);
  for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    if (std::find(moving.begin(), moving.end(), *it) == moving.end()) rest.push_back(*it);
  std::vector<Widget*> movingFront(moving.rbegin(), moving.rend());
  // in the stack's order already (moving came from selection(): sort it)
  std::sort(movingFront.begin(), movingFront.end(), [&](Widget* a, Widget* b) {
    return std::find(stack.begin(), stack.end(), a) > std::find(stack.begin(), stack.end(), b);
  });
  auto pos = below ? std::find(rest.begin(), rest.end(), below) : rest.end();
  rest.insert(pos, movingFront.begin(), movingFront.end());
  std::vector<Widget*> bottomToTop(rest.rbegin(), rest.rend());
  std::map<std::string, std::string> groupOf;
  if (!header)
    for (Widget* w : moving) groupOf[w->cfg.id] = target;
  restackAs(o, bottomToTop, groupOf);
}

// ── groups ──────────────────────────────────────────────────────────────────

std::string App::groupSelection() {
  auto sel = selection();
  if (sel.size() < 2) return {};
  const Output* o = sel.front()->output;
  const bool es = spanishUi();
  std::string name;
  for (int n = 1;; ++n) {
    name = std::format("{} {}", es ? "Grupo" : "Group", n);
    bool taken = false;
    for (auto& w : m_widgets) taken = taken || w->cfg.group == name;
    if (!taken) break;
  }
  // the members side by side in the stack, where the front one was
  const auto stack = stackOf(o);
  std::vector<Widget*> rest, members;
  for (Widget* w : stack) (std::find(sel.begin(), sel.end(), w) != sel.end() ? members : rest).push_back(w);
  size_t at = 0;
  for (Widget* w : stack)
    if (w == members.back()) break;
    else if (std::find(members.begin(), members.end(), w) == members.end()) ++at;
  std::vector<Widget*> next(rest.begin(), rest.begin() + static_cast<long>(at));
  next.insert(next.end(), members.begin(), members.end());
  next.insert(next.end(), rest.begin() + static_cast<long>(at), rest.end());
  std::map<std::string, std::string> groupOf;
  for (Widget* w : members) groupOf[w->cfg.id] = name;
  restackAs(o, next, groupOf);
  US_INFO("grouped {} widgets as \"{}\"", members.size(), name);
  return name;
}

void App::ungroupSelection() {
  auto sel = selection();
  if (sel.empty()) return;
  // a group picked whole comes apart; picked members just leave theirs
  std::set<std::string> ids;
  for (Widget* w : sel) {
    if (w->cfg.group.empty()) continue;
    const auto members = groupMembers(w->output, w->cfg.group);
    const bool whole = std::all_of(members.begin(), members.end(), [&](Widget* m) { return m->cfg.hidden || isSelected(m); });
    if (whole)
      for (Widget* m : members) ids.insert(m->cfg.id);
    else
      ids.insert(w->cfg.id);
  }
  if (ids.empty()) return;
  editBlocks([&](const std::string& id, const std::string& block) {
    return ids.count(id) ? Config::setBlockKey(block, "group", "") : block;
  });
}

void App::setHidden(const std::vector<Widget*>& ws, bool hidden) {
  std::set<std::string> ids;
  for (Widget* w : ws) ids.insert(w->cfg.id);
  // a hidden widget cannot stay picked on the desktop
  std::vector<std::string> keep;
  for (Widget* s : selection())
    if (!hidden || !ids.count(s->cfg.id)) keep.push_back(s->cfg.id);
  if (keep.empty() && hidden && !ws.empty()) keep.push_back(ws.front()->cfg.id);  // still in the panel
  editBlocks(
      [&](const std::string& id, const std::string& block) {
        return ids.count(id) ? Config::setBlockKey(block, "hidden", hidden ? "true" : "") : block;
      },
      keep);
}

void App::setLocked(const std::vector<Widget*>& ws, bool locked) {
  std::set<std::string> ids;
  for (Widget* w : ws) ids.insert(w->cfg.id);
  editBlocks([&](const std::string& id, const std::string& block) {
    return ids.count(id) ? Config::setBlockKey(block, "locked", locked ? "true" : "") : block;
  });
}

void App::deleteSelection() {
  auto sel = selection();
  if (sel.size() == 1) {
    if (!sel.front()->cfg.locked) removeWidget(*sel.front());
    return;
  }
  std::set<std::string> ids;
  for (Widget* w : sel)
    if (!w->cfg.locked) ids.insert(w->cfg.id);
  if (ids.empty()) return;
  std::vector<std::string> left = {""};  // the locked ones stay picked, or nothing
  for (Widget* w : sel)
    if (w->cfg.locked) left.push_back(w->cfg.id);
  m_pointerWidget = nullptr;
  editBlocks([&](const std::string& id, const std::string& block) { return ids.count(id) ? std::string() : block; }, left);
}

void App::duplicateSelection() {
  auto sel = selection();
  if (sel.size() == 1) {
    duplicateWidget(*sel.front());
    return;
  }
  if (sel.empty()) return;
  std::set<std::string> taken;
  for (auto& c : m_config.widgets) taken.insert(c.id);
  for (auto& w : m_widgets) taken.insert(w->cfg.group);
  auto fresh = [&](const std::string& base, const char* sep) {
    std::string id = base;
    for (int n = 2; taken.count(id); ++n) id = base + sep + std::to_string(n);
    taken.insert(id);
    return id;
  };
  // the copies stacked together right above the front one of the originals
  // (each group's copy a group of its own), the monitor renumbered for it
  const auto stack = stackOf(sel.front()->output);
  static const std::regex kNumbered(R"(-\d+$)");
  std::map<std::string, std::string> copyOf, groupCopy;
  Widget* front = nullptr;
  for (Widget* w : stack)
    if (std::find(sel.begin(), sel.end(), w) != sel.end()) {
      copyOf[w->cfg.id] = fresh(std::regex_replace(w->cfg.id, kNumbered, ""), "-");
      if (!w->cfg.group.empty() && !groupCopy.count(w->cfg.group)) groupCopy[w->cfg.group] = fresh(w->cfg.group, " ");
      front = w;
    }
  std::map<std::string, int> layerOf;
  int n = 0;
  for (Widget* w : stack) {
    layerOf[w->cfg.id] = ++n;
    if (w == front)
      for (Widget* s2 : stack)
        if (copyOf.count(s2->cfg.id)) layerOf[copyOf[s2->cfg.id]] = ++n;
  }
  std::vector<std::string> copies;
  for (const auto& [id, copy] : copyOf) copies.push_back(copy);
  editBlocks(
      [&](const std::string& id, const std::string& block) {
        auto it = layerOf.find(id);
        if (it == layerOf.end()) return block;  // another monitor's
        std::string b = block;
        for (Widget* w : stack)
          if (w->cfg.id == id && w->cfg.layer != it->second) b = Config::setBlockKey(b, "layer", std::to_string(it->second));
        if (!copyOf.count(id)) return b;
        Widget* w = nullptr;
        for (Widget* s2 : sel)
          if (s2->cfg.id == id) w = s2;
        WidgetConfig moved = w->cfg;  // 32 px down and right, so it is seen apart
        moved.x += 32;
        moved.y += 32;
        double* p = handlePoints(moved);
        for (int i = 0; i < handleCount(moved); ++i) p[2 * i] += 32, p[2 * i + 1] += 32;
        mapWidget(moved, w->toScreen.inverse());
        std::string copy = Config::retargetBlock(b, copyOf[id], moved.x, moved.y);
        copy = Config::setBlockKey(copy, "layer", std::to_string(layerOf[copyOf[id]]));
        if (moved.pinned) copy = Config::setBlockKey(copy, "pin", pinText(moved.pin));
        if (moved.meshed) copy = Config::setBlockKey(copy, "mesh", pointsText(moved.mesh, 2 * moved.meshN * moved.meshN));
        if (!w->cfg.group.empty()) copy = Config::setBlockKey(copy, "group", tomlString(groupCopy[w->cfg.group]));
        return b + "\n" + copy;
      },
      copies);
}

// ── names ───────────────────────────────────────────────────────────────────

void App::commitLayerRename() {
  if (m_layerRenaming.size() < 3) {
    m_layerRenaming.clear();
    return;
  }
  const bool group = m_layerRenaming[0] == 'g';
  const std::string key = m_layerRenaming.substr(2);
  std::string text = m_layerRenameText;
  while (!text.empty() && text.back() == ' ') text.pop_back();
  m_layerRenaming.clear();
  if (group) {
    if (text.empty() || text == key) return;
    if (m_foldedGroups.erase(key)) m_foldedGroups.insert(text);
    std::set<std::string> ids;
    for (Widget* w : groupMembers(m_uiOutput, key)) ids.insert(w->cfg.id);
    editBlocks([&](const std::string& id, const std::string& block) {
      return ids.count(id) ? Config::setBlockKey(block, "group", tomlString(text)) : block;
    });
  } else {
    editBlocks([&](const std::string& id, const std::string& block) {
      if (id != key) return block;
      return Config::setBlockKey(block, "name", text.empty() || text == id ? std::string() : tomlString(text));
    });
  }
  markEditDirty();
}

bool App::layerKey(uint32_t key) {
  if (key == KEY_ENTER || key == KEY_KPENTER) {
    commitLayerRename();
    return false;
  }
  if (key == KEY_ESC) {
    m_layerRenaming.clear();
    markEditDirty();
    return false;
  }
  if (key == KEY_BACKSPACE) {
    while (!m_layerRenameText.empty()) {
      const unsigned char c = static_cast<unsigned char>(m_layerRenameText.back());
      m_layerRenameText.pop_back();
      if ((c & 0xC0) != 0x80) break;
    }
    markEditDirty();
    return true;
  }
  if (!m_xkbState || modActive(XKB_MOD_NAME_CTRL)) return false;
  char buf[16] = {};
  const int n = xkb_state_key_get_utf8(m_xkbState, key + 8, buf, sizeof buf);
  if (n > 0 && static_cast<unsigned char>(buf[0]) >= 0x20 && buf[0] != 0x7f && m_layerRenameText.size() < 40) {
    m_layerRenameText += std::string(buf, static_cast<size_t>(n));
    markEditDirty();
    return true;
  }
  return false;
}

// ── moving, scaling and turning several widgets ─────────────────────────────

Box App::selectionBox() {
  int x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
  for (Widget* w : selection()) {
    const Box b = visualBox(w->cfg);
    x0 = std::min(x0, b.x), y0 = std::min(y0, b.y);
    x1 = std::max(x1, b.x + b.w), y1 = std::max(y1, b.y + b.h);
  }
  if (x0 > x1) return {};
  return {x0, y0, x1 - x0, y1 - y0};
}

int App::groupGripAt(double x, double y) {
  if (!multiSelected()) return -1;
  const Box b = selectionBox();
  for (int i = 0; i < 4; ++i) {
    const double cx = (i & 1) ? b.x + b.w : b.x, cy = (i & 2) ? b.y + b.h : b.y;
    if (std::abs(x - cx) <= 10 && std::abs(y - cy) <= 10) return i;
  }
  return -1;
}

bool App::inGroupKnob(double x, double y) {
  if (!multiSelected()) return false;
  const Box b = selectionBox();
  return std::hypot(x - (b.x + b.w / 2.0), y - (b.y - OverlayPass::kHandleGap)) <= OverlayPass::kHandleR + 6;
}

void App::groupDragBegin(Drag kind) {
  m_groupStart.clear();
  for (Widget* w : selection())
    if (!w->cfg.locked) m_groupStart.push_back({w->cfg.id, w->cfg});
  m_groupBox0 = selectionBox();
  // one undo step for the whole drag (dropped again if nothing moved)
  EditOp op;
  op.kind = "layout";
  op.block = Config::widgetBlocks(readFile(m_configPath));
  m_undo.push_back(op);
  m_redo.clear();
  m_lastOpKind.clear();
  m_drag = kind;
  m_pressX = m_px;
  m_pressY = m_py;
  const double cx = m_groupBox0.x + m_groupBox0.w / 2.0, cy = m_groupBox0.y + m_groupBox0.h / 2.0;
  m_groupAngle0 = std::atan2(m_py - cy, m_px - cx);
  m_rotAcc = 0;
  m_rotLast = m_groupAngle0 * 180 / std::numbers::pi;
  if (kind == Drag::GroupResize) {  // the opposite corner stays put
    const int c = m_gripCorner;
    m_groupAnchorX = (c & 1) ? m_groupBox0.x : m_groupBox0.x + m_groupBox0.w;
    m_groupAnchorY = (c & 2) ? m_groupBox0.y : m_groupBox0.y + m_groupBox0.h;
  }
}

void App::groupDragTo(double x, double y) {
  const Box b0 = m_groupBox0;
  const double cx = b0.x + b0.w / 2.0, cy = b0.y + b0.h / 2.0;
  double dx = 0, dy = 0, k = 1, turnBy = 0;
  if (m_drag == Drag::Move) {
    dx = std::round(x - m_pressX), dy = std::round(y - m_pressY);
    // the box as a whole snaps to the screen and the widgets left out
    m_guidesV.clear();
    m_guidesH.clear();
    if (m_snapOn != modActive(XKB_MOD_NAME_SHIFT) && m_selected && m_selected->output) {
      const Output* o = m_selected->output;
      std::vector<double> tx = {0, o->logicalW() / 2, o->logicalW()}, ty = {0, o->logicalH() / 2, o->logicalH()};
      for (Widget* w : stackOf(o)) {
        if (isSelected(w) || !w->surface || w->impl->fullscreen()) continue;
        const Box b = visualBox(w->cfg);
        tx.insert(tx.end(), {double(b.x), b.x + b.w / 2.0, double(b.x + b.w)});
        ty.insert(ty.end(), {double(b.y), b.y + b.h / 2.0, double(b.y + b.h)});
      }
      double d = 0, g = 0;
      const double nx = b0.x + dx, ny = b0.y + dy;
      if (snapAxis({nx, nx + b0.w / 2.0, nx + b0.w}, tx, 8.0, d, g)) dx += std::round(d), m_guidesV.push_back(static_cast<float>(g));
      if (snapAxis({ny, ny + b0.h / 2.0, ny + b0.h}, ty, 8.0, d, g)) dy += std::round(d), m_guidesH.push_back(static_cast<float>(g));
    }
  } else if (m_drag == Drag::GroupResize) {
    // one factor for both axes, so the composition keeps its proportions
    const double ox = m_groupAnchorX, oy = m_groupAnchorY;
    const double d0 = std::hypot(m_pressX - ox, m_pressY - oy);
    if (d0 < 1) return;
    const double vx = (m_pressX - ox) / d0, vy = (m_pressY - oy) / d0;
    k = std::max(0.05, ((x - ox) * vx + (y - oy) * vy) / d0);
    for (const auto& s : m_groupStart)  // never below a widget's least size
      k = std::max({k, 48.0 / std::max(1, s.cfg.width), 32.0 / std::max(1, s.cfg.height)});
  } else if (m_drag == Drag::GroupRotate) {
    const bool fine = modActive(XKB_MOD_NAME_SHIFT);
    const double now = std::atan2(y - cy, x - cx) * 180 / std::numbers::pi;
    m_rotAcc += normalizeDegrees(now - m_rotLast) * (fine ? 0.1 : 1.0);
    m_rotLast = now;
    double a = m_rotAcc;
    const double near15 = std::round(a / 15.0) * 15.0;
    if (m_snapOn && !fine && std::abs(a - near15) < 2.0) a = near15;
    turnBy = std::round(a * 10) / 10;
  }
  const double ax = m_groupAnchorX, ay = m_groupAnchorY, rad = turnBy * std::numbers::pi / 180;
  for (const auto& s : m_groupStart) {
    Widget* w = nullptr;
    for (auto& ww : m_widgets)
      if (ww->cfg.id == s.id) w = ww.get();
    if (!w) continue;
    WidgetConfig c = s.cfg;
    double mx = c.x + c.width / 2.0, my = c.y + c.height / 2.0;  // the box's centre
    auto map = [&](double& px, double& py) {
      if (m_drag == Drag::Move) px += dx, py += dy;
      else if (m_drag == Drag::GroupResize) px = ax + (px - ax) * k, py = ay + (py - ay) * k;
      else turn(px, py, cx, cy, rad);
    };
    map(mx, my);
    if (m_drag == Drag::GroupResize) {
      c.width = std::max(48, static_cast<int>(std::lround(c.width * k)));
      c.height = std::max(32, static_cast<int>(std::lround(c.height * k)));
    }
    if (m_drag == Drag::GroupRotate && !handPlaced(c)) c.rotation = normalizeDegrees(c.rotation + turnBy);
    c.x = static_cast<int>(std::lround(mx - c.width / 2.0));
    c.y = static_cast<int>(std::lround(my - c.height / 2.0));
    double* p = handlePoints(c);
    for (int i = 0; i < handleCount(c); ++i) map(p[2 * i], p[2 * i + 1]);
    w->cfg.x = c.x, w->cfg.y = c.y, w->cfg.width = c.width, w->cfg.height = c.height, w->cfg.rotation = c.rotation;
    std::copy(std::begin(c.pin), std::end(c.pin), std::begin(w->cfg.pin));
    std::copy(std::begin(c.mesh), std::end(c.mesh), std::begin(w->cfg.mesh));
    placeLayer(*w);
    w->needsRender = true;
    w->drewEmpty = false;
  }
  markEditDirty();
}

void App::groupDragEnd() {
  const Drag kind = m_drag;
  m_drag = Drag::None;
  m_guidesV.clear();
  m_guidesH.clear();
  std::vector<Widget*> moved;
  bool changed = false;
  for (const auto& s : m_groupStart)
    for (auto& w : m_widgets)
      if (w->cfg.id == s.id) {
        moved.push_back(w.get());
        changed = changed || w->cfg.x != s.cfg.x || w->cfg.y != s.cfg.y || w->cfg.width != s.cfg.width ||
                  w->cfg.height != s.cfg.height || std::abs(w->cfg.rotation - s.cfg.rotation) > 1e-6 ||
                  !std::equal(std::begin(s.cfg.pin), std::end(s.cfg.pin), std::begin(w->cfg.pin)) ||
                  !std::equal(std::begin(s.cfg.mesh), std::end(s.cfg.mesh), std::begin(w->cfg.mesh));
      }
  if (!changed) {  // a click: a selection, not an undo step
    if (!m_undo.empty() && m_undo.back().kind == "layout") m_undo.pop_back();
    m_groupStart.clear();
    return;
  }
  if (kind == Drag::Move && m_gridOn && !modActive(XKB_MOD_NAME_SHIFT)) {
    // the box's corner to the grid (an axis held by a guide keeps it)
    const Box b = selectionBox();
    const int g = m_config.gridSize;
    const int sx = snapToGrid(b.x, g) - b.x, sy = snapToGrid(b.y, g) - b.y;
    for (Widget* w : moved) {
      w->cfg.x += sx, w->cfg.y += sy;
      double* p = handlePoints(w->cfg);
      for (int i = 0; i < handleCount(w->cfg); ++i) p[2 * i] += sx, p[2 * i + 1] += sy;
      placeLayer(*w);
    }
  }
  writeGeometry(moved);
  m_groupStart.clear();
  markEditDirty();
}

void App::writeGeometry(const std::vector<Widget*>& ws) {
  std::map<std::string, const Widget*> byId;
  for (const Widget* w : ws) byId[w->cfg.id] = w;
  const std::string text = readFile(m_configPath);
  std::string out;
  for (const auto& b : Config::splitBlocks(Config::widgetBlocks(text))) {
    std::string nb = b;
    if (auto it = byId.find(Config::blockId(b)); it != byId.end()) {
      const WidgetConfig s = toStored(*it->second);  // the file keeps the block's own space
      nb = Config::setBlockKey(nb, "x", std::to_string(s.x));
      nb = Config::setBlockKey(nb, "y", std::to_string(s.y));
      nb = Config::setBlockKey(nb, "width", std::to_string(s.width));
      nb = Config::setBlockKey(nb, "height", std::to_string(s.height));
      if (std::abs(s.rotation) > 1e-6 || it->second->cfg.options.contains("rotation"))
        nb = Config::setBlockKey(nb, "rotation", degrees(s.rotation));
      if (s.pinned) nb = Config::setBlockKey(nb, "pin", pinText(s.pin));
      if (s.meshed) nb = Config::setBlockKey(nb, "mesh", pointsText(s.mesh, 2 * s.meshN * s.meshN));
      for (auto& c : m_config.widgets)
        if (c.id == s.id) c.x = s.x, c.y = s.y, c.width = s.width, c.height = s.height;
    }
    if (!out.empty()) out += "\n";
    out += nb;
    if (!out.ends_with("\n")) out += "\n";
  }
  if (!writeFileAtomic(m_configPath, Config::replaceWidgetBlocks(text, out))) US_WARN("could not save the moved widgets");
}

}  // namespace undershell
