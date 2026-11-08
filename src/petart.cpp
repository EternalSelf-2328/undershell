// SPDX-License-Identifier: GPL-3.0-or-later
// Original generated sprite artwork, embedded for offline use. The source
// atlases and reproducible import tool live in art/pets and tools/pack-pets.cpp.
#include "petart.hpp"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <algorithm>
#include <cstdio>

namespace undershell {
namespace {
constexpr int S = kPetPixels;
#include "petart.inc"  // generated from art/pets by tools/embed-pets.py
using Frames = std::array<PetPixels, kPetFamilySize>;

Frames decode(const unsigned char* png, size_t size) {
  Frames frames{};
  GdkPixbufLoader* loader = gdk_pixbuf_loader_new_with_type("png", nullptr);
  if (!loader) return frames;
  const bool fed = gdk_pixbuf_loader_write(loader, png, size, nullptr);
  const bool closed = gdk_pixbuf_loader_close(loader, nullptr);
  GdkPixbuf* pb = fed && closed ? gdk_pixbuf_loader_get_pixbuf(loader) : nullptr;
  if (pb && gdk_pixbuf_get_width(pb) == S*4 && gdk_pixbuf_get_height(pb) == S*4 &&
      gdk_pixbuf_get_n_channels(pb) == 4) {
    const auto* data = gdk_pixbuf_read_pixels(pb);
    const int stride = gdk_pixbuf_get_rowstride(pb);
    for (int i=0;i<kPetFamilySize;++i) for(int y=0;y<S;++y)
      std::copy_n(data+((i/4)*S+y)*stride+(i%4)*S*4,S*4,frames[i].data()+y*S*4);
  } else {
    std::fputs("undershell: could not decode embedded pet artwork\n", stderr);
  }
  g_object_unref(loader);
  return frames;
}

struct Artwork { Frames idle, stride, rest; };
const Artwork& artwork(bool chibi) {
  // Decode only once, without filesystem lookup or network access. Each
  // widget uploads a texture only when its discrete pose/blink/facing changes.
  if(chibi) {
    static const Artwork art{decode(kPetChibi_idle,sizeof kPetChibi_idle),
                             decode(kPetChibi_stride,sizeof kPetChibi_stride),
                             decode(kPetChibi_rest,sizeof kPetChibi_rest)};
    return art;
  }
  static const Artwork art{decode(kPetAtlas_idle,sizeof kPetAtlas_idle),
                           decode(kPetAtlas_stride,sizeof kPetAtlas_stride),
                           decode(kPetAtlas_rest,sizeof kPetAtlas_rest)};
  return art;
}

struct Eye { int x, y, width; };
constexpr std::array<Eye,kPetFamilySize> eyes{{
  {91,37,3},{85,31,3},{100,29,3},{30,44,3},{100,29,3},{103,28,3},{100,32,3},
  {108,64,3},{109,38,3},{88,31,3},{75,29,3},{55,31,4},{70,24,3},{65,19,3}
}};
void shutEye(PetPixels& pixels, Eye eye) {
  // Cover the eye glint with a short dark lid; never paint outside the figure.
  for(int x=eye.x-1;x<eye.x-1+eye.width;++x) {
    auto* p=pixels.data()+(eye.y*S+x)*4;
    if(p[3]) { p[0]=32; p[1]=28; p[2]=40; }
  }
}
const Frames& chibiBlinks() {
  // Dedicated eyelid artwork preserves the large, expressive chibi faces.
  static const Frames frames=decode(kPetChibi_blink,sizeof kPetChibi_blink);
  return frames;
}
void dot(PetPixels& p,int x,int y,std::uint32_t color) {
  if(x<=0 || x>=S-1 || y<=0 || y>=S-1) return;
  auto* v=p.data()+(y*S+x)*4;
  v[0]=color>>16; v[1]=color>>8; v[2]=color; v[3]=255;
}
} // namespace

PetPixels petPixels(PetKind kind, PetPose pose, bool frame, bool blink, bool flip) {
  const bool chibi=isChibiPet(kind);
  const int index=static_cast<int>(kind)-static_cast<int>(chibi?PetKind::LongChibi:PetKind::Long);
  if(index<0 || index>=kPetFamilySize) return {}; // Sprout has its original renderer.
  const auto& art=artwork(chibi);
  const bool resting=pose==PetPose::Sit || pose==PetPose::Sleep;
  const bool alternate=(frame && (pose==PetPose::Walk || pose==PetPose::Dance)) || pose==PetPose::Jump;
  PetPixels pixels=resting?art.rest[index]:(alternate?art.stride[index]:art.idle[index]);
  if(blink && !resting && !alternate) {
    if(chibi) pixels=chibiBlinks()[index];
    else shutEye(pixels,eyes[index]);
    if(kind==PetKind::Cerberus) { shutEye(pixels,{77,23,3}); shutEye(pixels,{110,48,3}); }
  }
  if(pose==PetPose::Jump || (pose==PetPose::Dance && frame)) {
    const auto original=pixels; pixels.fill(0);
    const int lift=pose==PetPose::Jump?3:2;
    for(int y=1;y<S-lift;++y)
      std::copy_n(original.data()+(y+lift)*S*4,S*4,pixels.data()+y*S*4);
  }
  if(pose==PetPose::Sleep) {
    // A discreet sleep indicator, outside the resting silhouette.
    for(int k=0;k<4;++k) {
      dot(pixels,17+k,25,0xc9b582); dot(pixels,20-k,26+k,0xc9b582);
      dot(pixels,17+k,30,0xc9b582);
    }
  } else if(pose==PetPose::Dance) {
    for(int bar=0;bar<3;++bar) for(int y=0;y<3+((bar+int(frame))%3)*2;++y)
      dot(pixels,119+bar*2,119-y,0xc9b582);
  }
  if(flip) for(int y=0;y<S;++y) for(int x=0;x<S/2;++x) for(int c=0;c<4;++c)
    std::swap(pixels[(y*S+x)*4+c],pixels[(y*S+S-1-x)*4+c]);
  return pixels;
}
} // namespace undershell
