// SPDX-License-Identifier: GPL-3.0-or-later
// The screensaver: after [screensaver] idle seconds without input (the
// compositor's ext-idle-notify, which respects idle inhibitors such as a video
// playing full screen), a look over the whole screen (the time in pixels, by
// default) and optionally a clock, over black or the darkened wallpaper, on
// the overlay layer of every screen until the next input. With music or a
// video playing it waits for the pause, unless while_playing = true.
//
// Its widgets are ordinary ones (config.cpp: screensaverWidgets) drawn by the
// same loop, but kept apart from the layout: the editor, the layers, the
// profiles and `msg json` never see them. Nothing runs while it is off but
// the idle notice itself.
#include "app.hpp"

#include <format>
#include <wayland-client.h>

#ifdef US_HAVE_IDLE_NOTIFY
#include "ext-idle-notify-v1-client-protocol.h"
#endif

namespace undershell {

void App::updateSaver(double now) {
  (void)now;
#ifdef US_HAVE_IDLE_NOTIFY
  // the idle notice, made again when the timeout changes
  const double want = m_config.screensaver.idle;
  if (m_idleNotifier && m_seat && want != m_idleNoteFor) {
    if (m_idleNote) ext_idle_notification_v1_destroy(m_idleNote);
    m_idleNote = nullptr;
    m_userIdle = false;
    if (want > 0) {
      static const ext_idle_notification_v1_listener kIdle = {
          [](void* d, ext_idle_notification_v1*) {
            auto* a = static_cast<App*>(d);
            a->m_userIdle = true;
            US_DEBUG("screensaver: idle");
          },
          [](void* d, ext_idle_notification_v1*) {
            auto* a = static_cast<App*>(d);
            a->m_userIdle = false;
            a->m_saverStop = true;
            US_DEBUG("screensaver: input again");
          }};
      m_idleNote = ext_idle_notifier_v1_get_idle_notification(m_idleNotifier, static_cast<uint32_t>(want * 1000), m_seat);
      ext_idle_notification_v1_add_listener(m_idleNote, &kIdle, this);
    }
    m_idleNoteFor = want;
  }
#endif
  if (m_saverStop) {
    m_saverStop = false;
    stopSaver();
  }
  // idle, nothing playing (or it may show anyway), and not in the editor
  if (m_userIdle && m_saver.empty() && !m_edit && m_config.screensaver.idle > 0 &&
      (m_config.screensaver.whilePlaying || !m_media.state().playing))
    startSaver(false);
}

void App::startSaver(bool manual) {
  if (!m_saver.empty() || !m_layerShell || toplevels()) return;  // (GNOME: no overlay layer)
  const ScreensaverConfig& sc = m_config.screensaver;
  for (auto& o : m_outputs) {
    if (o->name.empty() || (!sc.output.empty() && o->name != sc.output)) continue;
    const int ow = static_cast<int>(o->logicalW()), oh = static_cast<int>(o->logicalH());
    bool first = true;
    for (WidgetConfig& wc : screensaverWidgets(sc, ow, oh)) {
      auto w = std::make_unique<Widget>();
      wc.output = o->name;
      w->cfg = std::move(wc);
      w->impl = createWidget(w->cfg.type);
      if (!w->impl) continue;
      w->impl->configure(w->cfg, paletteFor(*w));
      w->output = o.get();
      w->saver = true;
      if (first && sc.background == "black") w->clear = Color{0, 0, 0, 1};  // the first one hides the desktop
      first = false;
      createSurface(*w);
      m_saver.push_back(std::move(w));
    }
  }
  m_saverSince = nowSeconds();
  m_saverHover = false;
  US_INFO("screensaver: on ({} surface(s){})", m_saver.size(), manual ? ", asked for" : "");
}

void App::stopSaver() {
  if (m_saver.empty()) return;
  for (auto& w : m_saver) destroySurface(*w);
  m_saver.clear();
  m_saverHover = false;
  US_INFO("screensaver: off");
}

// The pointer over the screensaver: no cursor, and moving or clicking (after a
// second, so the motion that came with it does not count) takes it down.
bool App::saverPointer(PointerEvent::Type type, wl_surface* s) {
  if (type == PointerEvent::Enter) {
    m_saverHover = false;
    for (auto& w : m_saver)
      if (w->surface == s) m_saverHover = true;
    if (m_saverHover && m_pointer) wl_pointer_set_cursor(m_pointer, m_pointerSerial, nullptr, 0, 0);
    return m_saverHover;
  }
  if (!m_saverHover) return false;
  if (type == PointerEvent::Leave) {
    m_saverHover = false;
    return true;
  }
  if (nowSeconds() - m_saverSince > 1.0) m_saverStop = true;
  return true;
}

std::string App::saverStatus() const {
  const ScreensaverConfig& sc = m_config.screensaver;
#ifndef US_HAVE_IDLE_NOTIFY
  if (sc.idle > 0) return "screensaver: built without ext-idle-notify";
#endif
  if (!m_saver.empty()) return std::format("screensaver: showing ({} surface(s))", m_saver.size());
  if (sc.idle <= 0) return "screensaver: off";
  if (!m_idleNotifier) return "screensaver: the compositor has no ext-idle-notify";
  return std::format("screensaver: after {:.0f} s idle{}{}", sc.idle, m_userIdle ? " (idle now, waiting for the music to stop)" : "",
                     sc.output.empty() ? "" : " on " + sc.output);
}

}  // namespace undershell
