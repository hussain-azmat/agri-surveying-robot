#!/usr/bin/env bash
# Builds and runs the host-side unit tests. Used by CI and works locally on Linux/macOS/MSYS2.
# Each function keeps its tests in Software/<Function>/Test/; Software/Common/Test holds the tiny shared harness.
# Only the plain-C++ half of each module is compiled here (the part under #ifdef ARDUINO needs the ESP32).
set -euo pipefail
cd "$(dirname "$0")/.."
SW=Software
mkdir -p build
INCLUDES=()
for d in "$SW"/*/Src "$SW"/Common/Test; do INCLUDES+=(-I "$d"); done
g++ -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter "${INCLUDES[@]}" \
  "$SW"/*/Test/*.cpp \
  "$SW"/GPS/Src/gps.cpp "$SW"/Path/Src/path.cpp "$SW"/Safety/Src/safety.cpp "$SW"/OTA/Src/ota_github.cpp \
  "$SW"/WiFi/Src/wifi_manager.cpp "$SW"/Logging/Src/github_log.cpp \
  -o build/host_tests
./build/host_tests "$@"
