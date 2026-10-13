// SPDX-License-Identifier: GPL-3.0-or-later
#include "net.hpp"

#include "common.hpp"

#include <curl/curl.h>
#include <filesystem>
#include <glib.h>
#include <mutex>

namespace undershell {

namespace {
constexpr const char* kUserAgent = "undershell/" US_VERSION " (+https://github.com/EternalSelf-2328/undershell)";

struct Sink {
  std::string* body;
  size_t max;
};

size_t write(char* p, size_t size, size_t n, void* ud) {
  auto* s = static_cast<Sink*>(ud);
  if (s->body->size() + size * n > s->max) return 0;  // too big: curl stops with an error
  s->body->append(p, size * n);
  return size * n;
}
}  // namespace

bool httpGet(const std::string& url, std::string& body, long timeoutSec, size_t maxBytes) {
  static std::once_flag init;
  std::call_once(init, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
  CURL* c = curl_easy_init();
  if (!c) return false;
  Sink sink{&body, maxBytes};
  curl_easy_setopt(c, CURLOPT_URL, url.c_str());
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
  // the web only: no file://, no other schemes, not even after a redirect
  curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
  curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
  curl_easy_setopt(c, CURLOPT_TIMEOUT, timeoutSec);
  curl_easy_setopt(c, CURLOPT_USERAGENT, kUserAgent);
  curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  const CURLcode rc = curl_easy_perform(c);
  long status = 0;
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
  curl_easy_cleanup(c);
  return rc == CURLE_OK && status >= 200 && status < 300;
}

std::string urlEncode(const std::string& s) {
  gchar* e = g_uri_escape_string(s.c_str(), nullptr, FALSE);
  std::string out(e);
  g_free(e);
  return out;
}

std::string cacheDir(const char* sub) {
  const char* xdg = std::getenv("XDG_CACHE_HOME");
  std::string d = std::string(xdg && *xdg ? xdg : expandHome("~/.cache")) + "/undershell/" + sub;
  std::error_code ec;
  std::filesystem::create_directories(d, ec);
  return d;
}

std::string hashOf(const std::string& s) {
  gchar* h = g_compute_checksum_for_string(G_CHECKSUM_SHA1, s.c_str(), -1);
  std::string out(h);
  g_free(h);
  return out.substr(0, 20);
}

}  // namespace undershell
