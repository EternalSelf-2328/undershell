// undershell: desktop widgets as a standalone Wayland client, independent of
// any shell or dotfiles. It lives beside Noctalia
// lives beside Noctalia (layer-shell bottom layer, Noctalia's palette and
// wallpaper_depth masks).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app.hpp"
#include "docgen.hpp"
#include "common.hpp"
#include "ipc.hpp"
#include "offscreen.hpp"

#include <clocale>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
  std::setlocale(LC_TIME, "");  // day and month names follow the system locale
  std::setlocale(LC_CTYPE, "");
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--debug")) undershell::g_logLevel = undershell::LogLevel::Debug;
    if (!std::strcmp(argv[i], "-h") || !std::strcmp(argv[i], "--help")) {
      std::puts("usage: undershell [--debug]            run the widgets\n"
                "       undershell --doc | --version | --snapshot DIR\n"
                "       undershell msg <command>       edit | edit-on | edit-off | demo | reset | reload | status | quit\n"
                "                                         set <id|all> <key> <value>   e.g. set all style orb\n"
                "                                         add <visualizer|clock|now_playing> [look] · remove <id>\n"
                "config: ~/.config/undershell/config.toml (applies live)");
      return 0;
    }
    if (!std::strcmp(argv[i], "--snapshot") && i + 1 < argc) return undershell::snapshotLooks(argv[i + 1]);
    if (!std::strcmp(argv[i], "--doc")) {
      std::fputs(undershell::optionsMarkdown().c_str(), stdout);
      return 0;
    }
    if (!std::strcmp(argv[i], "--version")) {
      std::puts("undershell " US_VERSION);
      return 0;
    }
    if (!std::strcmp(argv[i], "msg")) {
      if (i + 1 >= argc) {
        std::fputs("msg needs a command\n", stderr);
        return 2;
      }
      std::string cmd;
      for (int k = i + 1; k < argc; ++k) cmd += (k > i + 1 ? " " : "") + std::string(argv[k]);
      int rc = undershell::ipcSend(cmd);
      if (rc == 2) std::fputs("undershell is not running\n", stderr);
      return rc;
    }
  }
  undershell::App app;
  return app.run();
}
