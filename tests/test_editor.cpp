// Editor geometry: magnet snapping, screen bounds, grid.
#include "check.hpp"
#include "anchor.hpp"
#include "common.hpp"
#include "config.hpp"
#include "geom.hpp"
#include "snap.hpp"

#include <cmath>
#include <filesystem>

using namespace undershell;

int main() {
  double d = 0, g = 0;
  // a box whose left edge is 5 px right of the screen centre snaps onto it
  CHECK(snapAxis({965, 1065, 1165}, {0, 960, 1920}, 8, d, g));
  CHECK(d == -5 && g == 960);
  // the closest candidate wins (centre 3 px away beats edge 6 px away)
  CHECK(snapAxis({100, 203, 306}, {106, 200}, 8, d, g));
  CHECK(d == -3 && g == 200);
  // nothing within the threshold
  CHECK(!snapAxis({100, 150, 200}, {0, 960}, 8, d, g));

  // a widget flung far off-screen stays reachable (the bug that lost it)
  int x = -1072, y = 1056, w = 1088, h = 352;
  clampBox(x, y, w, h, 1920, 1080);
  CHECK(x + w >= 272 && y <= 1080 - 88);  // a quarter of it still visible
  // on-screen positions are untouched; partial overhang is allowed
  x = -100, y = 900, w = 800, h = 240;
  clampBox(x, y, w, h, 1920, 1080);
  CHECK(x == -100 && y == 900);
  // sizes are bounded
  x = 0, y = 0, w = 10, h = 5;
  clampBox(x, y, w, h, 1920, 1080);
  CHECK(w == 48 && h == 32);
  w = 9000, h = 9000;
  clampBox(x, y, w, h, 1920, 1080);
  CHECK(w == 3840 && h == 2160);

  CHECK(snapToGrid(23, 16) == 16);
  CHECK(snapToGrid(25, 16) == 32);
  CHECK(snapToGrid(-9, 16) == -16);
  CHECK(snapToGrid(7, 0) == 7);
  // rotation: unturned boxes are unchanged
  WidgetConfig c;
  c.x = 100, c.y = 200, c.width = 400, c.height = 100;
  Box b = surfaceBox(c);
  CHECK(b.x == 100 && b.y == 200 && b.w == 400 && b.h == 100);
  // a quarter turn swaps the bounds about the same centre (300, 250)
  c.rotation = 90;
  b = visualBox(c);
  CHECK(b.w == 100 && b.h == 400 && b.x == 250 && b.y == 50);
  // local <-> output are inverse, and +angle turns clockwise (y down)
  double ox = 0, oy = 0, lx = 0, ly = 0;
  toOutput(c, 400, 50, ox, oy);  // the right edge's middle ends up below the centre
  CHECK(std::abs(ox - 300) < 1e-9 && std::abs(oy - 450) < 1e-9);
  c.rotation = 33;
  toOutput(c, 17, 83, ox, oy);
  toLocal(c, ox, oy, lx, ly);
  CHECK(std::abs(lx - 17) < 1e-9 && std::abs(ly - 83) < 1e-9);
  CHECK(insideWidget(c, 300, 250));
  c.rotation = 45;  // the unturned corner is outside once turned
  CHECK(!insideWidget(c, 102, 202));
  CHECK(normalizeDegrees(190) == -170 && normalizeDegrees(-180) == 180 && normalizeDegrees(540) == 180);

  // perspective: the mapping is exact both ways; leaning back narrows the top
  {
    WidgetConfig w3;
    w3.x = 100, w3.y = 100, w3.width = 400, w3.height = 200;
    w3.tiltX = 30;
    w3.rotation = 10;
    CHECK(warped(w3) && rotated(w3));
    double qx[4], qy[4];
    w3.rotation = 0;
    widgetCorners(w3, qx, qy);
    CHECK(qx[1] - qx[0] < qx[2] - qx[3]);   // top edge shorter than the bottom
    CHECK(qy[3] - qy[0] < 200);             // and the box shorter
    w3.rotation = 10;
    w3.skewX = 15;
    double ox = 0, oy = 0, lx = 0, ly = 0;
    toOutput(w3, 37, 151, ox, oy);
    toLocal(w3, ox, oy, lx, ly);
    CHECK(std::abs(lx - 37) < 1e-6 && std::abs(ly - 151) < 1e-6);
    widgetCorners(w3, qx, qy);
    toOutput(w3, 400, 200, ox, oy);         // the homography lands on the corners
    CHECK(std::abs(ox - qx[2]) < 1e-6 && std::abs(oy - qy[2]) < 1e-6);
    const Box vb = visualBox(w3);
    for (int i = 0; i < 4; ++i) CHECK(qx[i] >= vb.x && qx[i] <= vb.x + vb.w && qy[i] >= vb.y && qy[i] <= vb.y + vb.h);
  }

  {
    // ── mesh warp ──
    WidgetConfig m;
    m.x = 100, m.y = 50, m.width = 300, m.height = 120;
    m.meshed = true;
    m.meshN = 3;
    const double qx[4] = {100, 400, 400, 100}, qy[4] = {50, 50, 170, 170};
    meshShape(qx, qy, 3, "flat", 0, m.mesh);
    // a flat mesh is the box itself, smooth or straight, inside and on the border
    for (bool smooth : {true, false}) {
      m.meshSmooth = smooth;
      for (double u : {0.0, 0.3, 0.5, 1.0})
        for (double v : {0.0, 0.7, 1.0}) {
          double x = 0, y = 0;
          meshEval(m, u, v, x, y);
          CHECK(std::abs(x - (100 + 300 * u)) < 1e-6 && std::abs(y - (50 + 120 * v)) < 1e-6);
        }
    }
    // the smooth surface passes through every point, also when bent
    m.meshSmooth = true;
    meshShape(qx, qy, 3, "bulge", 80, m.mesh);
    for (int j = 0; j < 3; ++j)
      for (int i = 0; i < 3; ++i) {
        double x = 0, y = 0;
        meshEval(m, i / 2.0, j / 2.0, x, y);
        CHECK(std::abs(x - m.mesh[2 * (j * 3 + i)]) < 1e-6 && std::abs(y - m.mesh[2 * (j * 3 + i) + 1]) < 1e-6);
      }
    // a bulge reaches past its corners' box; the surface box holds it all
    const Box vb = visualBox(m);
    CHECK(vb.x < 100 && vb.y < 50 && vb.x + vb.w > 400 && vb.y + vb.h > 170);
    // output -> widget -> output, on an arc
    meshShape(qx, qy, 3, "arc", 60, m.mesh);
    for (double u : {0.1, 0.5, 0.9})
      for (double v : {0.2, 0.8}) {
        double x = 0, y = 0, lx = 0, ly = 0;
        meshEval(m, u, v, x, y);
        toLocal(m, x, y, lx, ly);
        CHECK(std::abs(lx - u * m.width) < 0.5 && std::abs(ly - v * m.height) < 0.5);
        CHECK(insideWidget(m, x, y));
      }
    CHECK(!insideWidget(m, 10, 10));  // far off the surface
    // the arc bends the middle up, the corners stay where they are
    double mx = 0, my = 0;
    meshEval(m, 0.5, 0.0, mx, my);
    CHECK(my < 50 - 20);
    double cx[4], cy[4];
    widgetCorners(m, cx, cy);
    CHECK(cx[0] == 100 && cy[0] == 50 && cx[2] == 400 && cy[2] == 170);
    // straight: the edges between points are straight lines
    m.meshSmooth = false;
    double ax = 0, ay = 0, bx = 0, by = 0, hx = 0, hy = 0;
    meshEval(m, 0.0, 0.0, ax, ay);
    meshEval(m, 0.5, 0.0, bx, by);
    meshEval(m, 0.25, 0.0, hx, hy);
    CHECK(std::abs(hx - (ax + bx) / 2) < 1e-6 && std::abs(hy - (ay + by) / 2) < 1e-6);
    // resampling to 5 x 5 keeps the shape
    m.meshSmooth = true;
    WidgetConfig r = m;
    meshResample(m, 5, r.mesh);
    r.meshN = 5;
    for (double u : {0.0, 0.25, 0.5, 1.0}) {
      double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
      meshEval(m, u, 0.5, x1, y1);
      meshEval(r, u, 0.5, x2, y2);
      CHECK(std::abs(x1 - x2) < 1.5 && std::abs(y1 - y2) < 1.5);
    }
    // the mesh moves with the wallpaper across screen sizes
    const double before = m.mesh[8];
    mapWidget(m, SpaceMap{0.5, 10, 0.5, 20});
    CHECK(std::abs(m.mesh[8] - (before * 0.5 + 10)) < 1e-9);
    // the render size follows the shape's edges
    int fw = 0, fh = 0;
    meshShape(qx, qy, 3, "flat", 0, m.mesh);
    mapWidget(m, SpaceMap{});
    meshFitSize(m, fw, fh);
    CHECK(fw == 300 && fh == 120);
  }
  {
    // the shape modes in the config: new keys, and older blocks read as before
    const std::string dir = std::filesystem::temp_directory_path() / "undershell-test-shape";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::string path = dir + "/config.toml";
    writeFileAtomic(path,
                    "[general]\n\n[[widget]]\nid = \"a\"\nx = 10\ny = 20\nwidth = 100\nheight = 50\nwarp = \"mesh\"\n"
                    "mesh_grid = 4\nmesh_between = \"straight\"\n\n"
                    "[[widget]]\nid = \"b\"\npin_corners = true\n\n"
                    "[[widget]]\nid = \"c\"\ntilt_x = 20\n\n"
                    "[[widget]]\nid = \"d\"\ntilt_x = 20\nwarp = \"none\"\n");
    const Config cfg = Config::load(path);
    CHECK(cfg.widgets.size() == 4);
    const WidgetConfig& a = cfg.widgets[0];
    CHECK(a.meshed && !a.pinned && a.meshN == 4 && !a.meshSmooth);
    CHECK(a.mesh[0] == 10 && a.mesh[1] == 20 && a.mesh[2 * 15] == 110 && a.mesh[2 * 15 + 1] == 70);  // a flat grid on the box
    CHECK(cfg.widgets[1].pinned && !cfg.widgets[1].meshed);
    CHECK(cfg.widgets[2].tiltX == 20);                       // a tilt that is set: tilt mode
    CHECK(cfg.widgets[3].tiltX == 0 && !warped(cfg.widgets[3]));  // kept in the file, not applied
    // the visualizer's own "shape" (its bars) is not the warp mode
    writeFileAtomic(path, "[[widget]]\nid = \"v\"\ntype = \"visualizer\"\nshape = \"square\"\nwarp = \"mesh\"\n");
    const Config vc = Config::load(path);
    CHECK(vc.widgets.size() == 1 && vc.widgets[0].meshed);
    CHECK(VisualizerConfig::fromTable(vc.widgets[0].options).shape == "square");
    std::filesystem::remove_all(dir);
  }

  return TEST_RESULT();
}
