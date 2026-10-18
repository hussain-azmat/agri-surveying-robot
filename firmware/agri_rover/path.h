#pragma once
// Path recording, saving, loading and following (Stages 4 and 5).
//
//  RECORD  while driving manually, a point is kept every ~1.5 m or on a noticeable turn (both
//          tunable in config/rover.cfg). On stop the path is thinned (Douglas-Peucker), saved to
//          flash as paths/<name>.csv and queued for upload to the repo's paths/ folder.
//  FOLLOW  paths/<name>.csv is loaded (downloaded from the repo if not cached), and the pure
//          PathFollower (follow.h) steers the rover along it; see follow.h for the stop rules.
//
// Two kinds of caller, two kinds of function:
//   * control-task side: pathAutoStep / pathRecFeed. Never block, never touch flash or the network.
//   * main-loop side: everything else. May take the path lock, use flash and the network.
#include <Arduino.h>

#include "follow.h"
#include "state.h"

void pathBegin();

// ---- control-task side ---------------------------------------------------------------------
// One follower tick. Returns false (and leaves outputs untouched) if the path lock was busy this tick.
bool pathAutoStep(const GpsData& g, bool newFix, const ImuData& im, float dt, FollowCmd* cmd, FollowStatus* st);
// Call on every NEW GPS fix. Cheap; ignored unless recording.
void pathRecFeed(const GpsData& g, const ImuData& im, const RuntimeConfig& cfg);

// ---- main-loop side --------------------------------------------------------------------------
bool pathRecStart(const char** why);
// Stops, thins and saves. `requestedName` may be empty. On success *savedName is the final name.
bool pathRecStop(const char* requestedName, String* savedName, String* message);
int pathRecCount();

// Loads paths/<name>.csv into the follower (downloading it if it is not cached and we are online).
bool pathAutoPrepare(const char* name, String* message);
// Starts following from the nearest point and switches the rover to AUTO. Needs a fix and heading.
bool pathAutoBegin(const char** why);
void pathAutoStop();
const char* pathLoadedName();

// JSON array of {"name","bytes","pending"} for paths saved on the rover.
String pathListJson();
bool pathNameValid(const char* name);

// Uploads queued paths. Call from the main loop; does nothing unless online and idle.
void pathService(bool online, bool idle);
