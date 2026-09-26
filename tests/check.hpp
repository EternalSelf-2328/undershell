// Minimal test helpers: CHECK prints the failing expression and counts it.
#pragma once
#include <cstdio>

inline int g_failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)
#define TEST_RESULT() (g_failures == 0 ? 0 : 1)
