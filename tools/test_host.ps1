# Builds and runs the host-side unit tests (pure logic: GPS parsing, geometry, path
# following, safety rules, OTA version logic, config clamping). No ESP32 needed.
# Requires g++ (MSYS2 UCRT64 / MinGW on Windows, or any g++ on Linux/macOS).
param([string]$Filter = "")
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$fw = Join-Path $root "firmware\agri_rover"
$out = Join-Path $root "build"
New-Item -ItemType Directory -Force $out | Out-Null

$sources = @("nmea", "ubx", "geo", "follow", "fwversion", "rt_config", "safety", "accesscode") | ForEach-Object { Join-Path $fw "$_.cpp" }
$tests = Get-ChildItem (Join-Path $root "test") -Filter "*.cpp" | ForEach-Object { $_.FullName }

& g++ -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -I $fw -I (Join-Path $root "test") @tests @sources -o (Join-Path $out "host_tests.exe")
if ($LASTEXITCODE -ne 0) { throw "compile failed" }
if ($Filter) { & (Join-Path $out "host_tests.exe") $Filter } else { & (Join-Path $out "host_tests.exe") }
exit $LASTEXITCODE
