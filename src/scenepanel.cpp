// SPDX-License-Identifier: GPL-3.0-or-later
#include "app.hpp"
#include "wallset.hpp"

#include <charconv>
#include <filesystem>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>

namespace undershell {
namespace {

constexpr float kPanelWidth = 510, kPad = 14, kGap = 8, kRow = 34;

std::string choose(bool es, const char* en, const char* translated) {
  return es ? translated : en;
}

std::string displayBool(std::optional<bool> value, bool es) {
  return !value ? choose(es, "Any", "Cualquiera") : *value ? choose(es, "Yes", "Sí") : "No";
}

std::optional<bool> nextBool(std::optional<bool> value) {
  if (!value) return true;
  if (*value) return false;
  return {};
}

std::string workspaceOf(const SceneRule& rule) {
  return rule.workspaceIndex > 0 ? std::to_string(rule.workspaceIndex) : rule.workspace;
}

}  // namespace

void App::openScenes(int tab) {
  m_sceneTab = tab;
  m_sceneDraft.reset();
  m_sceneField.clear();
  m_sceneScroll = 0;
  m_sceneKeyboard = -1;
  m_scenePickingProfile = false;
  m_sceneAnimationInspector = false;
  m_sceneNotice.clear();
  m_sceneDelete = false;
  m_savesOpen = true;
  m_fontPickFor.clear();
  closeColorPick();
  m_wallsOpen = m_galleryOpen = m_helpOpen = m_layersOpen = false;
  refreshSaves();
  markEditDirty();
}

void App::layoutScenes(const Rect& anchor) {
  const float availableW = m_areaRight - m_areaLeft;
  const float w = std::max(32.0F, std::min(kPanelWidth, availableW));
  const int total = m_sceneTab == 0 ? static_cast<int>(std::ranges::count_if(m_widgets, [&](const auto& layer) {
                      return layer->output == m_uiOutput && layer->impl && !layer->impl->fullscreen() &&
                             !layer->impl->overlay();
                    })) : m_scenePickingProfile ? static_cast<int>(m_saves.size()) : static_cast<int>(m_config.sceneRules.size());
  const float availableH = m_areaBottom - m_areaTop;
  m_sceneVisibleRows = std::clamp(static_cast<int>((availableH - 168) / (kRow + kGap)), 1, 5);
  const bool editing = m_sceneTab == 1 && m_sceneDraft && !m_scenePickingProfile;
  const float h = editing ? std::min(407.0F, availableH) :
                  157 + std::max(1, std::min(total, m_sceneVisibleRows)) * (kRow + kGap);
  const int maxScroll = editing ? std::max(0, static_cast<int>(std::ceil((407 - h) / (kRow + kGap)))) :
                                  std::max(0, total - m_sceneVisibleRows);
  m_sceneScroll = std::clamp(m_sceneScroll, 0, maxScroll);
  const float x = std::clamp(anchor.x + (anchor.w - w) / 2, m_areaLeft, std::max(m_areaLeft, m_areaRight - w));
  const float y = m_barBelow ? std::max(m_areaTop, m_areaBottom - h) : m_areaTop;
  const Rect panel{x, y, w, h};
  m_ui.push_back({UiControl::Panel, panel, -1, "scene-panel"});

  auto button = [&](const char* key, Rect box, int idx = -1) {
    m_ui.push_back({UiControl::SceneAction, box, idx, key});
  };
  const float inner = w - 2 * kPad;
  const float cell = (inner - kGap) / 2;
  const float third = (inner - 2 * kGap) / 3;
  const float left = x + kPad, right = left + cell + kGap;
  button("composition", {left, y + 43, third, kRow});
  button("automation", {left + third + kGap, y + 43, third, kRow});
  button("profiles", {left + 2 * (third + kGap), y + 43, third, kRow});
  auto pair = [&](float yy, const char* a, const char* b) {
    button(a, {left, y + yy, cell, kRow});
    button(b, {right, y + yy, cell, kRow});
  };
  if (m_sceneTab == 0) {
    button("add-layer", {left, y + 89, third, kRow});
    button("import", {left + third + kGap, y + 89, third, kRow});
    button("export", {left + 2*(third + kGap), y + 89, third, kRow});
    int visible = 0;
    for (size_t i = 0; i < m_widgets.size(); ++i) {
      const auto& layer = m_widgets[i];
      if (layer->output != m_uiOutput || !layer->impl || layer->impl->fullscreen() || layer->impl->overlay()) continue;
      if (visible >= m_sceneScroll && visible < m_sceneScroll + m_sceneVisibleRows) {
        button("layer", {left, y + 135 + (visible - m_sceneScroll) * (kRow + kGap), inner, kRow},
               static_cast<int>(i));
      }
      ++visible;
    }
  } else if (!m_sceneDraft) {
    pair(89, "new", "pause");
    for (int i = m_sceneScroll; i < std::min(total, m_sceneScroll + m_sceneVisibleRows); ++i)
      button("edit", {left, y + 135 + (i - m_sceneScroll) * (kRow + kGap), inner, kRow}, i);
  } else if (m_scenePickingProfile) {
    pair(89, "back-to-rule", "no-profile");
    for (int i = m_sceneScroll; i < std::min(total, m_sceneScroll + m_sceneVisibleRows); ++i)
      button("pick-profile", {left, y + 135 + (i - m_sceneScroll) * (kRow + kGap), inner, kRow}, i);
  } else {
    auto field = [&](const char* key, float xx, float yy, float ww) {
      const float shifted = yy - m_sceneScroll * (kRow + kGap);
      if (shifted >= 87 && shifted + kRow <= h - 66) button(key, {xx, y + shifted, ww, kRow});
    };
    field("profile", left, 89, inner);
    field("wallpaper", left, 131, cell);
    field("clear-wallpaper", right, 131, cell);
    field("from", left, 173, cell);
    field("until", right, 173, cell);
    field("workspace", left, 215, cell);
    field("workspace-output", right, 215, cell);
    field("battery", left, 257, third);
    field("playing", left + third + kGap, 257, third);
    field("focus", left + 2 * (third + kGap), 257, third);
    if (m_sceneRule >= 0) {
      field("earlier", left, 299, third);
      field("later", left + third + kGap, 299, third);
      field("delete", left + 2 * (third + kGap), 299, third);
    }
    pair(h - 46, "cancel", "apply");
  }
}

std::string App::sceneControlText(const UiControl& control) const {
  const bool es = spanishUi();
  const auto& key = control.value;
  if (key == "composition") return choose(es, "Composition", "Composición");
  if (key == "automation") return choose(es, "Automatic rules", "Reglas automáticas");
  if (key == "profiles") return choose(es, "Profiles", "Perfiles");
  if (key == "add-layer") return choose(es, "Add layer…", "Añadir capa…");
  if (key == "import") return choose(es, "Import scene…", "Importar escena…");
  if (key == "export") return choose(es, "Export scene…", "Exportar escena…");
  if (key == "new") return choose(es, "Add rule", "Añadir regla");
  if (key == "pause") return m_rulesPaused ? choose(es, "Resume rules", "Reanudar reglas") : choose(es, "Pause rules", "Pausar reglas");
  if (key == "layer" && control.prop >= 0 && static_cast<size_t>(control.prop) < m_widgets.size()) {
    const auto& layer = *m_widgets[control.prop];
    return choose(es, "Animate · ", "Animar · ") + layerName(layer);
  }
  auto profileName = [&](const std::string& id) {
    if (id.empty()) return choose(es, "No profile", "Sin perfil");
    const auto match = std::ranges::find(m_saves, id, &SavedLayout::id);
    return match != m_saves.end() ? match->name : id;
  };
  if (key == "edit" && control.prop >= 0 && static_cast<size_t>(control.prop) < m_config.sceneRules.size()) {
    const auto& rule = m_config.sceneRules[control.prop];
    const auto title = rule.profile.empty() ? std::filesystem::path(rule.wallpaper).filename().string() : profileName(rule.profile);
    const auto time = rule.from != 0 || rule.until != 1440 ? sceneTime(rule.from) + "–" + sceneTime(rule.until) : std::string();
    auto label = std::format("{} · {}", control.prop + 1, title);
    for (const auto& detail : {time, workspaceOf(rule), rule.battery ? choose(es, "Power", "Energía") : "",
                               rule.playing ? choose(es, "Playback", "Reproducción") : "",
                               rule.focus ? choose(es, "Focus", "Concentración") : ""})
      if (!detail.empty()) label += " · " + detail;
    return label + " ›";
  }
  if (!m_sceneDraft) return {};
  const auto& rule = *m_sceneDraft;
  if (key == m_sceneField) return m_sceneBuffer + "▏";
  if (key == "back-to-rule") return choose(es, "Back to rule", "Volver a la regla");
  if (key == "no-profile") return choose(es, "Use no profile", "Usar sin perfil");
  if (key == "pick-profile" && control.prop >= 0 && static_cast<size_t>(control.prop) < m_saves.size())
    return m_saves[control.prop].name;
  if (key == "profile") return choose(es, "Profile · ", "Perfil · ") + profileName(rule.profile) + " ›";
  if (key == "wallpaper") return rule.wallpaper.empty() ? choose(es, "Choose wallpaper…", "Elegir fondo…")
                                                      : std::filesystem::path(rule.wallpaper).filename().string() + " …";
  if (key == "clear-wallpaper") return choose(es, "Remove wallpaper", "Quitar fondo");
  if (key == "from") return choose(es, "From · ", "Desde · ") + sceneTime(rule.from);
  if (key == "until") return choose(es, "Until · ", "Hasta · ") + sceneTime(rule.until);
  if (key == "workspace") return choose(es, "Workspace · ", "Espacio · ") +
      (workspaceOf(rule).empty() ? choose(es, "Any", "Cualquiera") : workspaceOf(rule));
  if (key == "workspace-output") return rule.workspaceOutput.empty() ? choose(es, "Focused output ›", "Pantalla con foco ›")
        : choose(es, "Output · ", "Pantalla · ") + rule.workspaceOutput + " ›";
  if (key == "battery") return choose(es, "Battery · ", "Batería · ") + displayBool(rule.battery, es);
  if (key == "playing") return choose(es, "Playback · ", "Música · ") + displayBool(rule.playing, es);
  if (key == "focus") return choose(es, "Focus · ", "Enfoque · ") + displayBool(rule.focus, es);
  if (key == "earlier") return choose(es, "Move up", "Subir");
  if (key == "later") return choose(es, "Move down", "Bajar");
  if (key == "delete") return m_sceneDelete ? choose(es, "Confirm removal", "Confirmar") : choose(es, "Remove rule", "Quitar regla");
  if (key == "cancel") return choose(es, "Cancel", "Cancelar");
  if (key == "apply") return choose(es, "Save rule", "Guardar regla");
  return {};
}

void App::saveSceneDraft() {
  if (!m_sceneDraft) return;
  const bool es = spanishUi();
  if (m_sceneDraft->profile.empty() && m_sceneDraft->wallpaper.empty()) {
    m_sceneNotice = choose(es, "Choose a profile or wallpaper first.", "Elige un perfil o un fondo.");
    return;
  }
  if (m_sceneDraft->from == m_sceneDraft->until) {
    m_sceneNotice = choose(es, "Start and end must differ.", "Usa horas de inicio y fin distintas.");
    return;
  }
  auto rules = m_sceneOriginal;
  if (m_sceneRule < 0) rules.push_back(*m_sceneDraft);
  else rules[static_cast<size_t>(m_sceneRule)] = *m_sceneDraft;
  std::string error;
  if (!storeSceneRules(m_configPath, m_sceneOriginal, rules, error)) {
    m_sceneNotice = error == "changed" ? choose(es, "The rules changed. Reopen this rule.", "Las reglas cambiaron. Vuelve a abrir esta regla.")
                                       : choose(es, "Could not save config.toml.", "No se pudo guardar config.toml.");
    return;
  }
  m_sceneDraft.reset();
  m_sceneNotice.clear();
  loadConfig();
}

bool App::sceneKey(uint32_t key) {
  if (m_sceneField.empty() || !m_sceneDraft) return false;
  if (key == KEY_ESC) {
    m_sceneField.clear();
    m_sceneNotice.clear();
  } else if (key == KEY_ENTER || key == KEY_KPENTER) {
    auto& rule = *m_sceneDraft;
    if (m_sceneField == "from" || m_sceneField == "until") {
      const std::string input = m_sceneBuffer.size() == 4 ? m_sceneBuffer.substr(0, 2) + ":" + m_sceneBuffer.substr(2) : m_sceneBuffer;
      const auto minute = input.empty() ? std::optional<int>(m_sceneField == "from" ? 0 : 1440) : sceneMinute(input);
      if (!minute || (m_sceneField == "from" && *minute == 1440)) {
        m_sceneNotice = choose(spanishUi(), "Enter an hour like 18:30.", "Escribe una hora como 18:30.");
        markEditDirty();
        return false;
      }
      if (m_sceneField == "from") rule.from = *minute;
      else rule.until = *minute;
    } else if (m_sceneField == "workspace") {
      int number = 0;
      auto [ptr, ec] = std::from_chars(m_sceneBuffer.data(), m_sceneBuffer.data()+m_sceneBuffer.size(), number);
      if (ec == std::errc() && ptr == m_sceneBuffer.data()+m_sceneBuffer.size() && number >= 1 && number <= 1000) {
        rule.workspaceIndex = number;
        rule.workspace.clear();
      } else {
        rule.workspaceIndex = 0;
        rule.workspace = m_sceneBuffer;
      }
    }
    m_sceneField.clear();
    m_sceneNotice.clear();
  } else if (key == KEY_BACKSPACE) {
    while (!m_sceneBuffer.empty()) {
      const auto c = static_cast<unsigned char>(m_sceneBuffer.back());
      m_sceneBuffer.pop_back();
      if ((c & 0xC0) != 0x80) break;
    }
  } else if (m_xkbState && !modActive(XKB_MOD_NAME_CTRL)) {
    char buf[16]{};
    const int count = xkb_state_key_get_utf8(m_xkbState, key + 8, buf, sizeof(buf));
    if (count > 0 && count < static_cast<int>(sizeof(buf)) && static_cast<unsigned char>(buf[0]) >= 32 && buf[0] != 127 &&
        m_sceneBuffer.size() < 160)
      m_sceneBuffer.append(buf, static_cast<size_t>(count));
  }
  markEditDirty();
  return key == KEY_BACKSPACE;
}

bool App::sceneNavigate(uint32_t key, bool backwards) {
  if (key == KEY_ESC && m_sceneTab == 1 && m_sceneDraft) {
    if (m_scenePickingProfile) m_scenePickingProfile = false;
    else m_sceneDraft.reset();
    m_sceneScroll = 0;
    m_sceneKeyboard = -1;
    m_sceneNotice.clear();
    markEditDirty();
    return true;
  }
  const int x = (key == KEY_RIGHT || key == KEY_L) - (key == KEY_LEFT || key == KEY_J);
  const int y = (key == KEY_DOWN || key == KEY_K) - (key == KEY_UP || key == KEY_I);
  auto focusable = [](const UiControl& c) {
    return c.type == UiControl::SceneAction || c.type == UiControl::SaveNew || c.type == UiControl::SceneBundle ||
           c.type == UiControl::SaveLoad || c.type == UiControl::SaveOverwrite || c.type == UiControl::SaveRename ||
           c.type == UiControl::SaveDelete;
  };
  if ((key == KEY_PAGEUP || key == KEY_PAGEDOWN) && m_sceneTab != 2) {
    m_sceneScroll = std::max(0, m_sceneScroll + (key == KEY_PAGEUP ? -1 : 1));
    m_sceneKeyboard = -1;
  } else if (x || y || key == KEY_TAB) {
    std::vector<int> choices;
    for (size_t i = 0; i < m_ui.size(); ++i)
      if (focusable(m_ui[i])) choices.push_back(static_cast<int>(i));
    if (choices.empty()) return true;
    const auto current = std::ranges::find(choices, m_sceneKeyboard);
    if (current == choices.end()) m_sceneKeyboard = backwards ? choices.back() : choices.front();
    else if (key == KEY_TAB) {
      const int i = static_cast<int>(current - choices.begin());
      const int n = static_cast<int>(choices.size());
      m_sceneKeyboard = choices[(i + n + (backwards ? -1 : 1)) % n];
    } else {
      const Rect from = m_ui[*current].r;
      float best = 1e9F;
      for (int i : choices) {
        const Rect to = m_ui[i].r;
        const float dx = to.x + 0.5F * to.w - from.x - 0.5F * from.w;
        const float dy = to.y + 0.5F * to.h - from.y - 0.5F * from.h;
        const float along = dx * x + dy * y;
        if (along < 4) continue;
        const float across = std::abs(dx * y + dy * x);
        const float score = along + across * 3;
        if (score >= best) continue;
        best = score;
        m_sceneKeyboard = i;
      }
    }
  } else if (key == KEY_ENTER || key == KEY_KPENTER) {
    if (m_sceneKeyboard >= 0 && static_cast<size_t>(m_sceneKeyboard) < m_ui.size() && focusable(m_ui[m_sceneKeyboard])) {
      const int index = m_sceneKeyboard;
      m_sceneKeyboard = -1;
      uiPress(index, m_ui[index].r.x);
    } else {
      m_sceneKeyboard = -1;
      for (size_t i = 0; i < m_ui.size(); ++i)
        if (focusable(m_ui[i])) {
          m_sceneKeyboard = static_cast<int>(i);
          break;
        }
    }
  } else return false;
  markEditDirty();
  return true;
}

bool App::sceneAction(const UiControl& control) {
  const auto& key = control.value;
  const bool es = spanishUi();
  m_sceneKeyboard = -1;
  if (key == "composition" || key == "automation" || key == "profiles") {
    if (!m_sceneField.empty()) {
      sceneKey(KEY_ENTER);
      if (!m_sceneField.empty()) return true;
    }
    m_sceneTab = key == "composition" ? 0 : key == "automation" ? 1 : 2;
    m_scenePickingProfile = false;
    m_sceneScroll = 0;
    if (m_sceneTab == 2) refreshSaves();
    markEditDirty();
    return true;
  }
  if (!m_sceneField.empty()) {
    sceneKey(KEY_ENTER);
    if (!m_sceneField.empty() && key != "cancel") return true;
  }
  if (key != "delete") m_sceneDelete = false;
  if (key == "add-layer") {
    m_savesOpen = false;
    m_galleryOpen = true;
  } else if (key == "import" || key == "export") {
    chooseSceneBundle(key == "import");
  } else if (key == "layer" && control.prop >= 0 && static_cast<size_t>(control.prop) < m_widgets.size()) {
    const auto id = m_widgets[control.prop]->cfg.id;
    selectIds({id});
    m_sceneAnimationInspector = true;
    m_savesOpen = false;
    m_inspScroll = 0;
  } else if (key == "pause") {
    sceneRulesCommand(m_rulesPaused ? "resume" : "pause");
  } else if (key == "new" || key == "edit") {
    m_sceneScroll = 0;
    m_sceneOriginal = m_config.sceneRules;
    m_sceneRule = key == "new" ? -1 : control.prop;
    if (m_sceneRule < 0 || static_cast<size_t>(m_sceneRule) >= m_sceneOriginal.size()) {
      m_sceneRule = -1;
      m_sceneDraft = SceneRule{};
    } else {
      m_sceneDraft = m_sceneOriginal[static_cast<size_t>(m_sceneRule)];
    }
    m_sceneNotice.clear();
  } else if (m_sceneDraft) {
    auto& rule = *m_sceneDraft;
    if (key == "cancel") {
      m_sceneDraft.reset();
      m_sceneField.clear();
      m_sceneNotice.clear();
    } else if (key == "apply") {
      saveSceneDraft();
    } else if (key == "profile") {
      if (m_saves.empty()) {
        m_sceneNotice = choose(es, "Save a layout under Profiles first.", "Guarda una disposición en la pestaña Perfiles.");
      } else {
        m_scenePickingProfile = true;
        m_sceneScroll = 0;
      }
    } else if (key == "back-to-rule" || key == "no-profile" || key == "pick-profile") {
      if (key == "no-profile") rule.profile.clear();
      if (key == "pick-profile" && control.prop >= 0 && static_cast<size_t>(control.prop) < m_saves.size())
        rule.profile = m_saves[control.prop].id;
      m_scenePickingProfile = false;
      m_sceneScroll = 0;
    } else if (key == "battery") rule.battery = nextBool(rule.battery);
    else if (key == "playing") rule.playing = nextBool(rule.playing);
    else if (key == "focus") rule.focus = nextBool(rule.focus);
    else if (key == "clear-wallpaper") rule.wallpaper.clear();
    else if (key == "wallpaper" && !m_picker.busy()) {
      FilePicker::Request request;
      request.title = choose(es, "Choose a still wallpaper", "Elegir un fondo fijo");
      request.filterName = choose(es, "Images", "Imágenes");
      request.multiple = false;
      if (!rule.wallpaper.empty()) request.startDir = std::filesystem::path(expandHome(rule.wallpaper)).parent_path().string();
      request.mimes = {"image/*"};
      request.patterns = {"*.png", "*.jpg", "*.jpeg", "*.webp", "*.avif"};
      setEditorAside(true);
      if (!m_picker.open(std::move(request), [this](std::vector<std::string> paths, std::string error) {
            setEditorAside(false);
            if (!error.empty()) m_sceneNotice = choose(spanishUi(), "The picker did not open.", "No se abrió el selector.");
            if (m_sceneDraft && !paths.empty()) {
              std::string why;
              const auto path = checkedWallpaperPath(paths.front(), why);
              if (path.empty() || isVideoPath(path)) m_sceneNotice = choose(spanishUi(), "Choose a still image.", "Elige una imagen fija.");
              else m_sceneDraft->wallpaper = path;
            }
            markEditDirty();
          })) setEditorAside(false);
    } else if (key == "workspace-output") {
      if (workspaceOf(rule).empty()) {
        m_sceneNotice = choose(es, "Set a workspace first to choose its output.", "Elige un espacio de trabajo primero.");
      } else {
        auto current = std::ranges::find_if(m_outputs, [&](const auto& output) { return output->name == rule.workspaceOutput; });
        if (current == m_outputs.end()) current = m_outputs.begin();
        else ++current;
        rule.workspaceOutput = current == m_outputs.end() ? std::string() : (*current)->name;
        m_sceneNotice.clear();
      }
    } else if (key == "from" || key == "until" || key == "workspace") {
      m_sceneField = key;
      m_sceneBuffer.clear();
      m_sceneNotice = key == "workspace" ? choose(es, "Workspace name or number; blank means any.", "Nombre o número del espacio. Vacío acepta cualquiera.")
                    : choose(es, "Type an hour such as 18:30, then Enter.", "Escribe una hora como 18:30 y presiona Enter.");
    } else if (key == "earlier" || key == "later" || key == "delete") {
      if (key == "delete" && !m_sceneDelete) {
        m_sceneDelete = true;
        markEditDirty();
        return true;
      }
      if (m_sceneRule >= 0 && static_cast<size_t>(m_sceneRule) < m_sceneOriginal.size()) {
        auto order = m_sceneOriginal;
        const int to = m_sceneRule + (key == "earlier" ? -1 : 1);
        if (key == "delete") order.erase(order.begin() + m_sceneRule);
        else if (to < 0 || static_cast<size_t>(to) >= order.size()) return true;
        else std::swap(order[m_sceneRule], order[to]);
        std::string error;
        if (storeSceneRules(m_configPath, m_sceneOriginal, order, error)) {
          m_sceneDraft.reset();
          m_sceneNotice.clear();
          loadConfig();
        } else {
          m_sceneNotice = choose(es, "Could not update the rules. Reopen the panel.", "No se pudieron actualizar las reglas. Vuelve a abrir el panel.");
        }
      }
    }
  }
  markEditDirty();
  return true;
}

}  // namespace undershell
