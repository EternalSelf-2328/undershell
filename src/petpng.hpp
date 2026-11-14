// SPDX-License-Identifier: GPL-3.0-or-later
// Only for the trusted, binary-alpha PNGs embedded with the pets. Cairo is
// already required; decoding these bytes needs no image-loader process or bus.
#pragma once

#include <cairo.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace undershell {

inline bool decodePetPng(const unsigned char* png, size_t size, int width, int height,
                         std::vector<std::uint8_t>& rgba) {
  struct Input { const unsigned char* data; size_t remaining; } input{png, size};
  const auto read = [](void* closure, unsigned char* out, unsigned int length) -> cairo_status_t {
    auto& in = *static_cast<Input*>(closure);
    if (length > in.remaining) return CAIRO_STATUS_READ_ERROR;
    std::memcpy(out, in.data, length);
    in.data += length;
    in.remaining -= length;
    return CAIRO_STATUS_SUCCESS;
  };
  cairo_surface_t* surface = cairo_image_surface_create_from_png_stream(read, &input);
  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS ||
      cairo_image_surface_get_width(surface) != width || cairo_image_surface_get_height(surface) != height ||
      cairo_image_surface_get_format(surface) != CAIRO_FORMAT_ARGB32) {
    cairo_surface_destroy(surface);
    return false;
  }
  cairo_surface_flush(surface);
  const auto* data = cairo_image_surface_get_data(surface);
  const int stride = cairo_image_surface_get_stride(surface);
  std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * height * 4);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      std::uint32_t px;
      std::memcpy(&px, data + static_cast<size_t>(y) * stride + x * 4, sizeof px);
      const auto alpha = static_cast<std::uint8_t>(px >> 24);
      if (alpha != 0 && alpha != 255) {
        cairo_surface_destroy(surface);
        return false;
      }
      auto* out = pixels.data() + (static_cast<size_t>(y) * width + x) * 4;
      // Binary alpha makes Cairo's premultiplied channels exact straight RGBA.
      out[0] = (px >> 16) & 0xff, out[1] = (px >> 8) & 0xff, out[2] = px & 0xff, out[3] = alpha;
    }
  cairo_surface_destroy(surface);
  rgba = std::move(pixels);
  return true;
}

}  // namespace undershell
