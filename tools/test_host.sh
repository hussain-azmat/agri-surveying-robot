#!/usr/bin/env bash
# Builds and runs the host-side unit tests. Used by CI and works locally on Linux/macOS/MSYS2.
set -euo pipefail
cd "$(dirname "$0")/.."
FW=firmware/agri_rover
mkdir -p build
g++ -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -I "$FW" -I test \
  test/*.cpp \
  "$FW"/gps.cpp "$FW"/path.cpp "$FW"/safety.cpp "$FW"/ota_github.cpp "$FW"/wifi_manager.cpp \
  -o build/host_tests
./build/host_tests "$@"
