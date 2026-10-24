// SPDX-License-Identifier: GPL-3.0-or-later
// A Wallpaper Engine wallpaper drawn by undershell (type = "scene"): a
// Workshop id or a folder with a project.json. A video plays through
// FFmpeg; a scene is unpacked by the confined undershell-scene helper into
// ~/.cache/undershell/scenes, its layers read from that (wescene.hpp) and
// drawn with their transforms, colour and blending. While it loads, the
// project's preview picture stands in. Everything runs off the frame:
// unpacking, reading and decoding on a thread, one texture uploaded per
// frame. Like the wallpaper widget it covers the screen under the others.
#pragma once

#include "effectchain.hpp"
#include "particles.hpp"
#include "gl.hpp"
#include "video.hpp"
#include "wallstore.hpp"
#include "wescene.hpp"
#include "widget.hpp"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace undershell {

class SceneWidget final : public WidgetImpl {
public:
  ~SceneWidget() override;
  void configure(const WidgetConfig& cfg, const NoctaliaState& noct) override;
  void tick(const TickContext& ctx) override;
  void draw(const DrawContext& ctx) override;
  [[nodiscard]] bool fullscreen() const override { return true; }
  [[nodiscard]] bool keepsResolution() const override { return true; }
  [[nodiscard]] bool animating(const TickContext& ctx) const override;
  [[nodiscard]] bool usesAudio() const override { return m_audio; }
  [[nodiscard]] std::string stillWallpaper() const override;
  [[nodiscard]] double nextWakeup(double now) const override;
  [[nodiscard]] int fps() const override { return 30; }
  // the person's own effects (used by layers too) change: read again
  [[nodiscard]] std::vector<std::string> watchedFiles() const override { return m_fx.empty() ? std::vector<std::string>{} : std::vector<std::string>{userEffectsDir() + "/"}; }
  void filesChanged() override;
  // what it shows, for the status and the inspector
  [[nodiscard]] std::string state() const;

  // where unpacked scenes go (one folder each) and their shared assets
  static std::string cacheDir();
  // the helper, beside the binary or installed; "" when there is none
  static std::string helperPath();

  struct Pixels {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;  // straight alpha
    int format = -1;            // the .tex format (9: one channel, its red the coverage)
  };
  // Reads what the helper wrote for one texture (`base` without extension):
  // .usimg (LZ4) or .png/.jpg. False when none is there or it is broken.
  static bool loadTexture(const std::string& base, Pixels& out);

private:
  struct Load {
    std::mutex m;
    bool done = false;
    std::string error;
    WeProject project;
    WeScene scene;
    std::map<std::string, Pixels> pixels;  // texture key -> pixels, waiting for upload
    std::vector<std::string> layerTex;     // per layer: its texture key ("" none)
    std::vector<std::string> layerMasks;   // per layer: its effect masks' key ("" none)
    std::vector<toml::table> layerFx;      // per layer: its effects' options, masks filled in
    std::string out;                       // where it was unpacked
  };
  // a layer drawn through undershell's effects: into its own texture first
  struct LayerFx {
    EffectChain chain;
    GLuint fbo = 0, tex = 0;
    int w = 0, h = 0;
    ~LayerFx();
  };
  void start(bool keepShowing = false);  // keepShowing: load beside the scene on screen, swap when done
  static void loadScene(const std::shared_ptr<Load>& load, const std::string& id, const std::map<std::string, std::string>& overrides,
                        const std::function<void(std::string)>& fail);
  void drawCover(GLuint tex, float texW, float texH, const DrawContext& ctx, float alpha);
  void drawLayers(const DrawContext& ctx, bool particles);
  void captureStill(const DrawContext& ctx);

  std::string m_scene, m_speedText;
  double m_speed = 1;
  std::shared_ptr<Load> m_load, m_next;
  std::map<std::string, std::string> m_overrides;  // property -> value from the config
  bool m_uploadAll = false;
  bool m_ready = false, m_uploading = false;
  WeProject m_project;
  WeScene m_model;
  std::map<std::string, std::pair<GLuint, std::array<int, 2>>> m_tex;  // uploaded
  std::vector<std::string> m_layerTex, m_layerMasks;
  std::map<size_t, std::unique_ptr<LayerFx>> m_fx;  // by layer index
  NoctaliaState m_noct;
  ParticleField m_pt;  // the scene's particle systems
  std::map<std::string, int> m_formats;  // texture key -> .tex format
  bool m_moving = false, m_audio = false;
  bool m_stillTried = false;
  std::string m_stillPath;
  std::shared_ptr<std::atomic<bool>> m_still = std::make_shared<std::atomic<bool>>(false);  // written
  VideoSource m_video;
  WallpaperFade m_preview;
  Program m_prog;
  double m_lastTick = -1;
  double m_dt = 0;
};

}  // namespace undershell
