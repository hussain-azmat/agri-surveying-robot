Path  -  record a route, save it, follow it
===========================================

WHAT IT DOES
  RECORD: arm the rover, press Start under "Record a path" on the control page, drive the route by hand, press "Stop & save".
  The route is thinned (Douglas-Peucker, 0.4 m), saved in the rover's flash as paths/<name>.csv and uploaded to the repo's
  paths/ folder when the rover is stopped and online (the upload needs a GitHub token). "Load" can also download a path
  you committed to paths/ by hand (no token needed on a public repo).
  FOLLOW (AUTO mode): the follower steers along the saved points. It takes the median of 3 GPS fixes, consumes at most one
  waypoint per new fix, accepts a waypoint inside 4 m, measures cross-track error (xte_m) against a window of segments,
  and can resume from the nearest segment. It STOPS, disarmed with a stated reason, at the end of the path, when it leaves
  the path (3 consecutive bad fixes), when GPS is lost, when it makes no progress (stuck), or when the control page goes quiet.
  Paths are validated before use (2-2000 points, no jump over 500 m, total length at least 2 m); a bad file never reaches the motors.

FOLDER
  Src/path.h, path.cpp          geometry, the follower, record / save / load / upload
  Test/test_geo_follow.cpp      laptop tests: geometry, a closed-loop SIMULATION of the follower with GPS noise and outliers
  Bin/                          no stand-alone image: path logic needs GPS, IMU and motors together (see Rover)
  (path file format and the example square: paths/README.md at the repo root)

HONEST STATUS
  Tuned only in simulation. On the real rover calibrate turn_full_dps (config/rover.cfg) and measure the real cross-track error.
  GPS accuracy is about 2.5 m, so paths are metre-accurate at best.

PASS CRITERIA
  - Laptop: the unit tests pass, including the simulated square and loop (max cross-track error is printed).
  - Rover (docs/BRINGUP.md, Stage 4-5): replay a recorded 20 m square, wheels on the ground, clear sky; report max and mean xte_m,
    end-point error, and that every fault stops the rover.
