// SPDX-License-Identifier: GPL-3.0-or-later
#include "net.hpp"

#include "common.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <curl/curl.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
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

// the address is somewhere on the internet, not this machine or its network
bool publicAddress(const sockaddr* sa) {
  if (sa->sa_family == AF_INET) {
    const uint32_t a = ntohl(reinterpret_cast<const sockaddr_in*>(sa)->sin_addr.s_addr);
    const auto in = [&](uint32_t net, int bits) { return (a >> (32 - bits)) == (net >> (32 - bits)); };
    return !(in(0x00000000, 8) || in(0x0A000000, 8) || in(0x7F000000, 8) || in(0xA9FE0000, 16) || in(0xAC100000, 12) ||
             in(0xC0A80000, 16) || in(0x64400000, 10) || in(0xE0000000, 4) || in(0xF0000000, 4));
  }
  if (sa->sa_family == AF_INET6) {
    const in6_addr& a = reinterpret_cast<const sockaddr_in6*>(sa)->sin6_addr;
    if (IN6_IS_ADDR_V4MAPPED(&a)) {
      sockaddr_in v4{};
      v4.sin_family = AF_INET;
      std::memcpy(&v4.sin_addr, a.s6_addr + 12, 4);
      return publicAddress(reinterpret_cast<const sockaddr*>(&v4));
    }
    return !(IN6_IS_ADDR_LOOPBACK(&a) || IN6_IS_ADDR_UNSPECIFIED(&a) || IN6_IS_ADDR_LINKLOCAL(&a) || IN6_IS_ADDR_SITELOCAL(&a) ||
             IN6_IS_ADDR_MULTICAST(&a) || (a.s6_addr[0] & 0xFE) == 0xFC);  // fc00::/7, unique local
  }
  return false;
}

curl_socket_t openPublic(void*, curlsocktype, curl_sockaddr* addr) {
  if (!publicAddress(&addr->addr)) return CURL_SOCKET_BAD;
  return socket(addr->family, addr->socktype, addr->protocol);
}

size_t write(char* p, size_t size, size_t n, void* ud) {
  auto* s = static_cast<Sink*>(ud);
  if (s->body->size() + size * n > s->max) return 0;  // too big: curl stops with an error
  s->body->append(p, size * n);
  return size * n;
}
}  // namespace

bool httpGet(const std::string& url, std::string& body, long timeoutSec, size_t maxBytes, bool publicOnly) {
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
  if (publicOnly) curl_easy_setopt(c, CURLOPT_OPENSOCKETFUNCTION, openPublic);  // every hop, redirects too
  const CURLcode rc = curl_easy_perform(c);
  long status = 0;
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
  curl_easy_cleanup(c);
  return rc == CURLE_OK && status >= 200 && status < 300;
}

std::string readRegularFile(const std::string& path, size_t maxBytes) {
  const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) return {};
  std::string out;
  struct stat st{};
  if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && static_cast<size_t>(st.st_size) <= maxBytes) {
    out.resize(static_cast<size_t>(st.st_size));
    size_t got = 0;
    while (got < out.size()) {
      const ssize_t n = read(fd, out.data() + got, out.size() - got);
      if (n <= 0) break;
      got += static_cast<size_t>(n);
    }
    out.resize(got);
  }
  close(fd);
  return out;
}

std::string urlEncode(const std::string& s) {
  gchar* e = g_uri_escape_string(s.c_str(), nullptr, FALSE);
  std::string out(e);
  g_free(e);
  return out;
}

std::string cacheDir(const char* sub) {
  const char* xdg = std::getenv("XDG_CACHE_HOME");
  const std::string root = std::string(xdg && *xdg ? xdg : expandHome("~/.cache")) + "/undershell";
  const std::string d = root + "/" + sub;
  std::error_code ec;
  std::filesystem::create_directories(d, ec);
  // what was listened to, where the weather is asked for: the user's alone
  std::filesystem::permissions(root, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ec);
  return d;
}

std::string hashOf(const std::string& s) {
  gchar* h = g_compute_checksum_for_string(G_CHECKSUM_SHA1, s.c_str(), -1);
  std::string out(h);
  g_free(h);
  return out.substr(0, 20);
}

}  // namespace undershell
