#pragma once
// Minimal dependency-free test harness for the host-side (laptop / CI) tests.
#include <math.h>
#include <stdio.h>

#include <vector>

struct TestCase {
  const char* name;
  void (*fn)();
};
std::vector<TestCase>& testRegistry();
extern int g_failures;

struct TestRegistrar {
  TestRegistrar(const char* n, void (*f)()) { testRegistry().push_back({n, f}); }
};

#define TEST(name)                          \
  static void name();                       \
  static TestRegistrar registrar_##name(#name, name); \
  static void name()

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      g_failures++;                                                    \
    }                                                                  \
  } while (0)

#define CHECK_NEAR(a, b, tol)                                                              \
  do {                                                                                     \
    const double va_ = (a), vb_ = (b);                                                     \
    if (!(fabs(va_ - vb_) <= (tol))) {                                                     \
      printf("    FAIL %s:%d: %s ~= %s  (got %.9g vs %.9g, tol %g)\n", __FILE__, __LINE__, #a, #b, va_, vb_, (double)(tol)); \
      g_failures++;                                                                        \
    }                                                                                      \
  } while (0)
