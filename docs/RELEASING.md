# Releasing firmware (OTA)

```
change code → python tools/bump_version.py patch → git commit → git push
                                     │
       GitHub Actions: tests → compile → size check against the OTA slot → write ota/ → commit
                                     │
       each rover, at power-up once online, then every 5 min while STOPPED: reads ota/version.txt → newer? → downloads → verifies MD5 → reboots → stays disarmed
```

## Rules the system enforces

* **Newer, not merely different.** Versions are `MAJOR.MINOR.PATCH`, compared numerically (`0.10.0` is newer than `0.9.0`).
  A repo version that is equal or older is ignored. CI refuses to publish a version older than the current one.
* **No bump, no publish.** If code changed but `FW_VERSION` did not, CI builds and tests it and prints a notice; `ota/` is untouched.
* **Verified before it can boot.** `ota/version.txt` carries the image's MD5 and size. The rover streams the download into the
  *inactive* OTA slot and only switches to it if the byte count and MD5 both match. Any failure (network, size, MD5, flash)
  aborts and the running firmware is untouched.
* **Never while the rover can move.** Checks run at power-up and while disarmed; the flash is re-checked against "still
  disarmed" before the reboot.
* **Automatic by default, manual on request.** The first check happens as soon as the rover is online with a valid clock after
  power-up. With `auto_update=on` (default, `config/rover.cfg`) it installs a newer version by itself. With `auto_update=off` it only
  reports "update available" and waits for you to press **Install update** on the control page. **Check for update** always only looks.
* **After an update:** the rover reports the new version on the control page and stays **disarmed**. A person arms it.
* **Fits the slot.** `tools/publish_ota.py` refuses an image bigger than `app0` in `partitions.csv`.

## Doing a release

```bash
python tools/bump_version.py patch          # 0.1.0 -> 0.1.1   (minor / major / x.y.z also work)
git add -A && git commit -m "What changed"
git push
```

Watch the *firmware* workflow in the Actions tab. When it finishes, `ota/version.txt` shows the new version and MD5.
Rovers pick it up within `ota_interval_s` (default 5 minutes) of being stopped and online, or immediately via
*Check for update* on the control page.

**Roll out carefully:** there is no automatic rollback (stock Arduino builds do not enable the bootloader feature).
Flash one rover first, confirm it behaves, then let the rest update. To pull back a bad release, publish a *newer* version
that reverts the change.

## The first image

The very first firmware has to be flashed over USB (`tools/build_firmware.py`, then upload with Arduino IDE or
`arduino-cli upload`). The partition table is part of that first flash. After that, everything is OTA. CI publishes
`v0.1.0` to `ota/` on the first push, so a freshly USB-flashed rover reports "up to date".

## Why the repo may need a token

A private repo needs a fine-grained token (Contents: read, and write for uploads) on each rover, entered on the setup
page. A public repo can be read without one, but uploads always need write access. The token never appears in the repo,
the serial console or the logs.

## Keeping TLS working

Rovers trust the root CAs in `firmware/agri_rover/certs.h` and have no insecure fallback. GitHub changes its certificate
authorities occasionally, so the weekly `tls-roots` workflow checks that GitHub's live chain still validates against them
and fails *before* anything breaks. If it fails: `python tools/gen_certs.py <ca-bundle.pem>`, bump the version, release.
