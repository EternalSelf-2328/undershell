// Editor geometry: magnet snapping, screen bounds, grid.
#include "check.hpp"
#include "snap.hpp"

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
  return TEST_RESULT();
}
