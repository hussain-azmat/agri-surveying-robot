# paths/

Recorded and hand-made paths. The rover reads `paths/<name>.csv` and follows it in auto mode.

## Format

```
lat,lon
31.5204000,74.3587000
31.5204200,74.3587300
...
```

* One point per line, decimal degrees (WGS84), 7 decimals is about 1 cm.
* First line may be the header `lat,lon`. Extra columns are ignored. `#` lines are comments.
* 2 to 2000 points; no jump between points longer than 500 m; total length at least 2 m.
* A bad file is rejected with a message on the control page; it never reaches the motors.

## How paths get here

* **Recorded**: arm the rover, press *Start* under "Record a path", drive the route, press *Stop & save*.
  The path is thinned (Douglas-Peucker, 0.4 m) and uploaded here automatically when the rover is stopped and
  online, as `paths/<name>.csv`.
* **Generated**: any tool can write this format (for example from a map). Commit it here, then use
  *Load* on the control page: the rover downloads it if it does not have it yet.

`example_square.csv` is a 20 m square near the Lahore test coordinates, useful for checking the file format and
for a first replay. **Change the coordinates to your own test field before driving it.**
