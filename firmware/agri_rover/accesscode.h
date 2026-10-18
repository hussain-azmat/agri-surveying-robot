#pragma once
// PURE: no Arduino dependencies. Unit-tested on a laptop (test/test_misc.cpp), and mirrored by
// tools/ap_password.py so a sticker printed from a rover's MAC always matches what the rover derives.
//
// The access code protects the rover's own WiFi network and the control page on shared networks.
// It is derived from the chip's MAC and AP_PASSWORD_SALT, so it can be printed on a label without
// storing anything. HONEST LIMIT: anyone who knows the salt (it is in the source) and the MAC (which the
// rover broadcasts) can compute it, so it keeps casual users out; it is not a substitute for choosing
// your own code on the setup page (which replaces it), and the salt should be changed per deployment.
#include <stddef.h>

// `macHex12` is the 12 upper-case hex digits of the MAC, no separators. `out` needs 11 bytes.
void deriveAccessCode(const char* salt, const char* macHex12, char* out);
