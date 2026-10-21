# Builds and runs the host-side unit tests (pure logic: GPS parsing, geometry, path
# following, safety rules, OTA version logic, config clamping). No ESP32 needed.
# Requires g++ (MSYS2 UCRT64 / MinGW on Windows, or any g++ on Linux/macOS).
# Each function keeps its tests in Software\<Function>\Test; Software\Common\Test holds the shared harness.
param([string]$Filter = "")
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$sw = Join-Path $root "Software"
$out = Join-Path $root "build"
New-Item -ItemType Directory -Force $out | Out-Null

$includes = @()
Get-ChildItem $sw -Directory | ForEach-Object {
    $src = Join-Path $_.FullName "Src"
    if (Test-Path $src) { $includes += "-I"; $includes += $src }
}
$includes += "-I"; $includes += (Join-Path $sw "Common\Test")

$modules = @("GPS\Src\gps", "Path\Src\path", "Safety\Src\safety", "OTA\Src\ota_github", "WiFi\Src\wifi_manager", "Logging\Src\github_log")
$sources = $modules | ForEach-Object { Join-Path $sw "$_.cpp" }
$tests = Get-ChildItem $sw -Recurse -Filter "*.cpp" | Where-Object { $_.DirectoryName -match "\Test$" } | ForEach-Object { $_.FullName }

& g++ -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter @includes @tests @sources -o (Join-Path $out "host_tests.exe")
if ($LASTEXITCODE -ne 0) { throw "compile failed" }
if ($Filter) { & (Join-Path $out "host_tests.exe") $Filter } else { & (Join-Path $out "host_tests.exe") }
exit $LASTEXITCODE
