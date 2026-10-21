Common  -  settings and test harness shared by every function
=============================================================

  Src/config.h      EVERYTHING you might tune or rewire in one place: the pin map (checked at compile time: the build
                    fails if two functions share a GPIO or a motor enable pin is a boot / flash / input-only pin),
                    FW_VERSION (the number the over-the-air updater compares), the GitHub owner / repo / branch, speed and
                    timeout limits, the serial output switches. It contains NO secrets (no WiFi password, no token).
  Test/mini_test.h  a 40-line test framework (TEST(name) { CHECK(...); }), no installation needed
  Test/main.cpp     runs every TEST in the program; "bash tools/test_host.sh [name-filter]"

  The tests of each function are in Software/<Function>/Test/. To add a test, put a test_xxx.cpp file in that folder.
