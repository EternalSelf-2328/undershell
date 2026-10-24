// SPDX-License-Identifier: GPL-3.0-or-later
// undershell-script: runs the scripts of a Wallpaper Engine scene
// (SceneScript, JavaScript) for undershell's scene widget, mostly clocks and
// dates on text layers. Scripts are code from strangers, so they run here:
// in quickjs-ng with none of its file, process or network modules, under a
// memory limit and a time limit per call, in a process that has given up
// every file access (Landlock) once started. It talks to undershell over
// its standard input and output, one JSON object per line:
//
//   {"op":"load","id":N,"source":"…","value":"…","props":{…}}  -> {"id":N,"ok":true,"value":"…"}
//   {"op":"update","runtime":seconds,"dt":seconds,"ids":[N…]}  -> {"values":{"N":"…",…}}
//
// What a script may use is a small part of Wallpaper Engine's SceneScript,
// written here from its public description: engine (runtime, frametime,
// timeOfDay, screen and canvas size), createScriptProperties(), Vec2/Vec3,
// thisLayer, shared, localStorage (in memory), console (silent), and the
// standard JavaScript library (Date, Math, Intl where built in).
#include <quickjs.h>

#include <nlohmann/json.hpp>

#include <linux/landlock.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <regex>
#include <string>

using json = nlohmann::json;

namespace {

const char* kPrelude = R"(
"use strict";
class Vec2 {
  constructor(x = 0, y = x) { this.x = +x; this.y = +y; }
  add(v) { return typeof v === "number" ? new Vec2(this.x + v, this.y + v) : new Vec2(this.x + v.x, this.y + v.y); }
  subtract(v) { return typeof v === "number" ? new Vec2(this.x - v, this.y - v) : new Vec2(this.x - v.x, this.y - v.y); }
  multiply(v) { return typeof v === "number" ? new Vec2(this.x * v, this.y * v) : new Vec2(this.x * v.x, this.y * v.y); }
  divide(v) { return typeof v === "number" ? new Vec2(this.x / v, this.y / v) : new Vec2(this.x / v.x, this.y / v.y); }
  length() { return Math.hypot(this.x, this.y); }
  normalize() { const l = this.length() || 1; return new Vec2(this.x / l, this.y / l); }
  copy() { return new Vec2(this.x, this.y); }
  toString() { return this.x + " " + this.y; }
}
class Vec3 {
  constructor(x = 0, y = x, z = x) { this.x = +x; this.y = +y; this.z = +z; }
  add(v) { return typeof v === "number" ? new Vec3(this.x + v, this.y + v, this.z + v) : new Vec3(this.x + v.x, this.y + v.y, this.z + v.z); }
  subtract(v) { return typeof v === "number" ? new Vec3(this.x - v, this.y - v, this.z - v) : new Vec3(this.x - v.x, this.y - v.y, this.z - v.z); }
  multiply(v) { return typeof v === "number" ? new Vec3(this.x * v, this.y * v, this.z * v) : new Vec3(this.x * v.x, this.y * v.y, this.z * v.z); }
  divide(v) { return typeof v === "number" ? new Vec3(this.x / v, this.y / v, this.z / v) : new Vec3(this.x / v.x, this.y / v.y, this.z / v.z); }
  length() { return Math.hypot(this.x, this.y, this.z); }
  normalize() { const l = this.length() || 1; return new Vec3(this.x / l, this.y / l, this.z / l); }
  copy() { return new Vec3(this.x, this.y, this.z); }
  toString() { return this.x + " " + this.y + " " + this.z; }
}
globalThis.Vec2 = Vec2;
globalThis.Vec3 = Vec3;
globalThis.engine = {
  runtime: 0, frametime: 0, timeOfDay: 0, userProperties: {},
  screenResolution: new Vec2(1920, 1080), canvasSize: new Vec2(1920, 1080),
  isDesktopDevice() { return true; }, isMobileDevice() { return false; }, isWallpaper() { return true; },
  isScreensaver() { return false; }, isRunningInEditor() { return false; }, isPortrait() { return false; }, isLandscape() { return true; },
  registerAudioBuffers() { return { left: new Array(64).fill(0), right: new Array(64).fill(0), average: new Array(64).fill(0) }; },
  setTimeout(f) { return 0; }, setInterval(f) { return 0; }, clearTimeout() {}, clearInterval() {},
};
globalThis.createScriptProperties = function () {
  const values = {};
  const b = { finish() { return values; } };
  for (const m of ["addCheckbox", "addSlider", "addCombo", "addText", "addTextInput", "addColor", "addFile", "addDirectory"])
    b[m] = function (o) { if (o && typeof o.name === "string") values[o.name] = o.value; return b; };
  return b;
};
globalThis.shared = {};
globalThis.localStorage = (() => { const m = new Map(); return { get: k => m.get(k), set: (k, v) => { m.set(k, v); }, delete: k => m.delete(k), clear: () => m.clear() }; })();
globalThis.console = { log() {}, info() {}, warn() {}, error() {}, debug() {} };
globalThis.input = { cursorWorldPosition: new Vec3(0, 0, 0), cursorScreenPosition: new Vec2(0, 0), cursorLeftDown: false };
globalThis.WEMath = { lerp: (a, b, t) => a + (b - a) * t, clamp: (v, lo, hi) => Math.min(hi, Math.max(lo, v)), smoothStep: (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); }, mix: (a, b, t) => a + (b - a) * t, deg2rad: Math.PI / 180, rad2deg: 180 / Math.PI };
globalThis.WEVector = { Vec2, Vec3 };
globalThis.WEColor = { rgb2hsv: c => c, hsv2rgb: c => c };
globalThis.__mods = { WEMath: globalThis.WEMath, WEVector: globalThis.WEVector, WEColor: globalThis.WEColor };
)";

double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

double g_deadline = 0;  // a call past this is stopped
int interrupt(JSRuntime*, void*) { return g_deadline > 0 && now() > g_deadline ? 1 : 0; }

// a module made into a script that hands back what it exports
std::string wrap(std::string src) {
  static const std::regex kImportAll(R"re(import\s+\*\s+as\s+(\w+)\s+from\s+['"]([\w/.-]+)['"]\s*;?)re");
  static const std::regex kImportSome(R"re(import\s*\{([^}]*)\}\s*from\s+['"]([\w/.-]+)['"]\s*;?)re");
  static const std::regex kImportBare(R"re(import\s+['"][^'"]*['"]\s*;?)re");
  static const std::regex kExportDefault(R"re(\bexport\s+default\s+)re");
  static const std::regex kExport(R"re(\bexport\s+(?=(function|var|let|const|class|async)\b))re");
  src = std::regex_replace(src, kImportAll, "const $1 = (globalThis.__mods['$2'] || {});");
  src = std::regex_replace(src, kImportSome, "const {$1} = (globalThis.__mods['$2'] || {});");
  src = std::regex_replace(src, kImportBare, "");
  src = std::regex_replace(src, kExportDefault, "const __default = ");
  src = std::regex_replace(src, kExport, "");
  return "(function () {\n" + src +
         "\n;return { update: typeof update === 'function' ? update : undefined, init: typeof init === 'function' ? init : undefined,"
         " props: typeof scriptProperties !== 'undefined' ? scriptProperties : undefined };\n})()";
}

struct Script {
  JSValue update = JS_UNDEFINED, init = JS_UNDEFINED, props = JS_UNDEFINED, layer = JS_UNDEFINED;
  std::string value;
  bool broken = false;
};

std::string text(JSContext* ctx, JSValueConst v) {
  size_t n = 0;
  const char* s = JS_ToCStringLen(ctx, &n, v);
  if (!s) return {};
  std::string out(s, std::min<size_t>(n, 4096));
  JS_FreeCString(ctx, s);
  return out;
}

std::string lastError(JSContext* ctx) {
  JSValue e = JS_GetException(ctx);
  std::string s = text(ctx, e);
  JS_FreeValue(ctx, e);
  return s.empty() ? "it threw" : s;
}

// value = fn(value), within the time limit; false when it threw or ran too long
bool call(JSContext* ctx, Script& s, JSValueConst fn, double ms) {
  if (!JS_IsFunction(ctx, fn)) return true;
  JSValue global = JS_GetGlobalObject(ctx);
  JS_SetPropertyStr(ctx, global, "thisLayer", JS_DupValue(ctx, s.layer));
  JS_FreeValue(ctx, global);
  JSValue arg = JS_NewStringLen(ctx, s.value.data(), s.value.size());
  g_deadline = now() + ms / 1000;
  JSValue r = JS_Call(ctx, fn, JS_UNDEFINED, 1, &arg);
  g_deadline = 0;
  JS_FreeValue(ctx, arg);
  if (JS_IsException(r)) {
    std::string why = lastError(ctx);
    (void)why;
    return false;
  }
  if (!JS_IsUndefined(r) && !JS_IsNull(r)) s.value = text(ctx, r);
  else {  // a script that sets thisLayer.text instead of returning it
    JSValue t = JS_GetPropertyStr(ctx, s.layer, "text");
    if (JS_IsString(t)) s.value = text(ctx, t);
    JS_FreeValue(ctx, t);
  }
  JS_FreeValue(ctx, r);
  return true;
}

void confine() {
  prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
  const long abi = syscall(SYS_landlock_create_ruleset, nullptr, 0, LANDLOCK_CREATE_RULESET_VERSION);
  if (abi < 1) return;
  landlock_ruleset_attr attr{};
  attr.handled_access_fs = LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_READ_FILE |
                           LANDLOCK_ACCESS_FS_READ_DIR | LANDLOCK_ACCESS_FS_REMOVE_DIR | LANDLOCK_ACCESS_FS_REMOVE_FILE |
                           LANDLOCK_ACCESS_FS_MAKE_CHAR | LANDLOCK_ACCESS_FS_MAKE_DIR | LANDLOCK_ACCESS_FS_MAKE_REG |
                           LANDLOCK_ACCESS_FS_MAKE_SOCK | LANDLOCK_ACCESS_FS_MAKE_FIFO | LANDLOCK_ACCESS_FS_MAKE_BLOCK |
                           LANDLOCK_ACCESS_FS_MAKE_SYM;
  const int rs = static_cast<int>(syscall(SYS_landlock_create_ruleset, &attr, sizeof(attr), 0));
  if (rs < 0) return;
  // no rule at all: no file can be opened from here on
  if (syscall(SYS_landlock_restrict_self, rs, 0) == 0) std::fputs("confined\n", stderr);
  close(rs);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--check") return 0;
  JSRuntime* rt = JS_NewRuntime();
  JS_SetMemoryLimit(rt, 64u << 20);
  JS_SetMaxStackSize(rt, 1u << 20);
  JS_SetInterruptHandler(rt, interrupt, nullptr);
  JSContext* ctx = JS_NewContext(rt);  // the standard library only: no std/os modules
  JSValue pre = JS_Eval(ctx, kPrelude, std::strlen(kPrelude), "<prelude>", JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(pre)) {
    std::fprintf(stderr, "prelude: %s\n", lastError(ctx).c_str());
    return 1;
  }
  JS_FreeValue(ctx, pre);
  // the local time zone, read now: once confined, /etc/localtime is out of reach
  tzset();
  const char* warm = "new Date().getTimezoneOffset()";
  JS_FreeValue(ctx, JS_Eval(ctx, warm, std::strlen(warm), "<tz>", JS_EVAL_TYPE_GLOBAL));
  confine();
  std::map<int, Script> scripts;
  std::ios::sync_with_stdio(false);
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.size() > (1u << 20)) continue;
    const json in = json::parse(line, nullptr, false);
    if (!in.is_object()) continue;
    const std::string op = in.value("op", std::string());
    json out;
    if (op == "load") {
      const int id = in.value("id", -1);
      Script s;
      s.value = in.value("value", std::string());
      const std::string src = wrap(in.value("source", std::string()));
      s.layer = JS_NewObject(ctx);
      JS_SetPropertyStr(ctx, s.layer, "text", JS_NewStringLen(ctx, s.value.data(), s.value.size()));
      JS_SetPropertyStr(ctx, s.layer, "visible", JS_TRUE);
      JSValue global = JS_GetGlobalObject(ctx);
      JS_SetPropertyStr(ctx, global, "thisLayer", JS_DupValue(ctx, s.layer));
      JS_FreeValue(ctx, global);
      g_deadline = now() + 0.5;
      JSValue mod = JS_Eval(ctx, src.data(), src.size(), "<scene script>", JS_EVAL_TYPE_GLOBAL);
      g_deadline = 0;
      out["id"] = id;
      if (JS_IsException(mod)) {
        out["ok"] = false;
        out["error"] = lastError(ctx);
        s.broken = true;
      } else {
        s.update = JS_GetPropertyStr(ctx, mod, "update");
        s.init = JS_GetPropertyStr(ctx, mod, "init");
        s.props = JS_GetPropertyStr(ctx, mod, "props");
        // the scene's own values for its properties
        if (in.contains("props") && in["props"].is_object() && JS_IsObject(s.props))
          for (const auto& [k, v] : in["props"].items()) {
            const std::string text = v.dump();
            JSValue jv = JS_ParseJSON(ctx, text.data(), text.size(), "<prop>");
            if (!JS_IsException(jv)) JS_SetPropertyStr(ctx, s.props, k.c_str(), jv);
          }
        const bool ok = call(ctx, s, s.init, 200);
        out["ok"] = ok;
        if (!ok) s.broken = true;
        out["value"] = s.value;
      }
      JS_FreeValue(ctx, mod);
      scripts[id] = s;
    } else if (op == "update") {
      JSValue global = JS_GetGlobalObject(ctx);
      JSValue eng = JS_GetPropertyStr(ctx, global, "engine");
      const double t = in.value("runtime", 0.0);
      JS_SetPropertyStr(ctx, eng, "runtime", JS_NewFloat64(ctx, t));
      JS_SetPropertyStr(ctx, eng, "frametime", JS_NewFloat64(ctx, in.value("dt", 0.0)));
      JS_SetPropertyStr(ctx, eng, "timeOfDay", JS_NewFloat64(ctx, in.value("timeofday", 0.0)));
      JS_FreeValue(ctx, eng);
      JS_FreeValue(ctx, global);
      json values = json::object();
      for (const auto& idv : in.value("ids", json::array())) {
        if (!idv.is_number_integer()) continue;
        auto it = scripts.find(idv.get<int>());
        if (it == scripts.end() || it->second.broken) continue;
        if (!call(ctx, it->second, it->second.update, 30)) it->second.broken = true;  // stopped or threw: no more calls
        values[std::to_string(it->first)] = it->second.value;
      }
      out["values"] = values;
    } else {
      continue;
    }
    std::cout << out.dump() << '\n' << std::flush;
  }
  for (auto& [id, s] : scripts) JS_FreeValue(ctx, s.update), JS_FreeValue(ctx, s.init), JS_FreeValue(ctx, s.props), JS_FreeValue(ctx, s.layer);
  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);
  return 0;
}
