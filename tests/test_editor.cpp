// Editor geometry: magnet snapping, screen bounds, grid.
#include "check.hpp"
#include "geom.hpp"
#include "snap.hpp"

#include <cmath>

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

  return TEST_RESULT();
}
