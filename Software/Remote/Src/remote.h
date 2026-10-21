#pragma once
#ifdef ARDUINO
#include <Arduino.h>
#include <WebServer.h>

// The web server: control page, JSON API and (via wifi_manager) the setup page.
//
// Security model, kept deliberately simple:
//  * Clients on the rover's OWN network are already authenticated by its WPA2 password.
//  * Clients on a shared WiFi must send the access code (query parameter `k`). Repeated wrong
//    codes lock that out for a minute. State-changing calls are POST only.
//  * Every authorised request doubles as a heartbeat: AUTO mode stops if the page goes quiet.

void remoteBegin();
void remoteUpdate();  // handleClient(): call every loop iteration

bool remoteAuthorized(WebServer& s);
void remoteDenied(WebServer& s);
#endif
