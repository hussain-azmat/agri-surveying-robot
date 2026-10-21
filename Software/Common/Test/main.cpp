#include <stdio.h>
#include <string.h>

#include "mini_test.h"

std::vector<TestCase>& testRegistry() {
  static std::vector<TestCase> r;
  return r;
}
int g_failures = 0;

int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : nullptr;
  int run = 0, failedTests = 0;
  for (const TestCase& t : testRegistry()) {
    if (filter && !strstr(t.name, filter)) continue;
    const int before = g_failures;
    t.fn();
    run++;
    if (g_failures != before) {
      failedTests++;
      printf("[FAIL] %s\n", t.name);
    } else {
      printf("[ ok ] %s\n", t.name);
    }
  }
  printf("\n%d tests, %d failed, %d failed checks\n", run, failedTests, g_failures);
  return g_failures == 0 ? 0 : 1;
}
