#pragma once
// The real-time control task. It owns the sensors (IMU, GPS, battery), applies EVERY failsafe via
// safetyEvaluate(), runs the path follower and drives the motors, at 100 Hz on core 1 with its
// own watchdog.
//
// It never touches flash, WiFi or the network, so a slow GitHub upload, an OTA flash or a stalled
// web request can never delay a stop: if the web side hangs, commands stop arriving and this task
// stops the rover by itself within the command timeout.
#include <Arduino.h>

void controlStart();
