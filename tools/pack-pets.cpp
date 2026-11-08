// SPDX-License-Identifier: GPL-3.0-or-later
// Import the generated 4 x 4 sprite atlases onto the runtime pixel grid.
// Run from the repository root; see art/pets/README.md.
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  if(argc>2 || (argc==2 && std::string(argv[1])!="chibi"))
    throw std::runtime_error("usage: pack-pets [chibi]");
  const bool chibi=argc==2;
  const std::string root=chibi?"art/pets/chibi/":"art/pets/";
  constexpr int S = 128, edge = 8, ground = 120;
  // writes the atlases (art/pets/<pose>.png); the build embeds them
  // (tools/embed-pets.py), so there is nothing else to keep in sync
  for (const auto name : {"idle", "stride", "rest", "blink"}) {
    if(!chibi && std::string(name)=="blink") continue;
    const std::string path = root + "source/" + name + ".png";
    GdkPixbuf* source = gdk_pixbuf_new_from_file(path.c_str(), nullptr);
    if (!source || !gdk_pixbuf_get_has_alpha(source)) throw std::runtime_error("missing RGBA source: " + path);
    const int w = gdk_pixbuf_get_width(source), h = gdk_pixbuf_get_height(source);
    const int stride = gdk_pixbuf_get_rowstride(source);
    const auto* data = gdk_pixbuf_read_pixels(source);
    GdkPixbuf* atlas = gdk_pixbuf_new(GDK_COLORSPACE_RGB, true, 8, S*4, S*4);
    gdk_pixbuf_fill(atlas, 0);
    auto* output = gdk_pixbuf_get_pixels(atlas);
    const int outStride = gdk_pixbuf_get_rowstride(atlas);
    for (int i = 0; i < 14; ++i) {
      const int x0 = (i%4)*w/4, y0 = (i/4)*h/4;
      const int cw = (i%4+1)*w/4-x0, ch = (i/4+1)*h/4-y0;
      // Reject alpha fringes and isolated generation speckles. Keep detached
      // components large enough to be deliberate ornaments or weapons.
      std::vector<bool> seen(cw*ch), keep(cw*ch);
      for (int y = 0; y < ch; ++y) for (int x = 0; x < cw; ++x) {
        const int seed = y*cw+x;
        if (seen[seed] || data[(y+y0)*stride+(x+x0)*4+3]<224) continue;
        std::vector<int> component{seed}; seen[seed]=true;
        for (size_t n=0;n<component.size();++n) {
          const int p=component[n], px=p%cw, py=p/cw;
          for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
            const int nx=px+dx,ny=py+dy,q=ny*cw+nx;
            if(nx<0 || ny<0 || nx>=cw || ny>=ch || seen[q]) continue;
            if(data[(ny+y0)*stride+(nx+x0)*4+3]>=224) { seen[q]=true; component.push_back(q); }
          }
        }
        if(component.size()>=48) for(int p:component) keep[p]=true;
      }
      int left=cw,top=ch,right=-1,bottom=-1;
      for(int y=0;y<ch;++y) for(int x=0;x<cw;++x) if(keep[y*cw+x]) {
        left=std::min(left,x); right=std::max(right,x); top=std::min(top,y); bottom=std::max(bottom,y);
      }
      if(right<left) throw std::runtime_error("empty character");
      const int bw=right-left+1,bh=bottom-top+1;
      double scale=std::min(double(S-2*edge)/bw,double(ground-edge)/bh);
      if(std::string(name)=="rest") scale*=chibi?0.90:(i>=12?0.78:(i>=10?0.86:0.92));
      const int dw=std::lround(bw*scale),dh=std::lround(bh*scale);
      const int ox=(S-dw)/2,oy=ground-dh;
      for(int y=0;y<dh;++y) for(int x=0;x<dw;++x) {
        const int sx=left+std::min(bw-1,int((x+0.5)*bw/dw));
        const int sy=top+std::min(bh-1,int((y+0.5)*bh/dh));
        if(!keep[sy*cw+sx]) continue;
        const auto* pixel=data+(sy+y0)*stride+(sx+x0)*4;
        auto* dst=output+((i/4)*S+oy+y)*outStride+((i%4)*S+ox+x)*4;
        std::copy_n(pixel,3,dst); dst[3]=255;
      }
    }
    const std::string native=root+name+".png";
    if(!gdk_pixbuf_save(atlas,native.c_str(),"png",nullptr,nullptr)) throw std::runtime_error("PNG write failed");
    std::printf("%s written\n",native.c_str());
    g_object_unref(atlas); g_object_unref(source);
  }
}
