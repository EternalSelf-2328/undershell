// SPDX-License-Identifier: GPL-3.0-or-later
// Plain HTTPS fetches for the feeds that need the network (lyrics, covers,
// the weather), run on worker threads only, and the cache folder they share.
#pragma once

#include <cstddef>
#include <string>

namespace undershell {

// A blocking GET (worker threads only): http(s) only, redirects too, a time
// limit and a size limit; true for a 2xx answer.
// `publicOnly`: refuse to connect to this machine or the local network (for
// addresses that came from other programs, like a song's cover art).
bool httpGet(const std::string& url, std::string& body, long timeoutSec = 10, size_t maxBytes = 16u << 20, bool publicOnly = false);
// A small regular file (not a device, a pipe or a folder), at most `maxBytes`;
// "" otherwise.
std::string readRegularFile(const std::string& path, size_t maxBytes = 16u << 20);
std::string urlEncode(const std::string& s);
// ~/.cache/undershell/<sub> (created)
std::string cacheDir(const char* sub);
// a short, stable file name for a key (SHA-1, 20 hex digits)
std::string hashOf(const std::string& s);

}  // namespace undershell
