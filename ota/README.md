# ota/

What every rover updates itself from. **Do not edit these files by hand**: CI writes them.

| File | What it is |
|---|---|
| `version.txt` | `version=`, `md5=` and `size=` of the image. The rover verifies the download against the MD5 before it can boot. |
| `firmware.bin` | The compiled application image (`agri_rover.ino.bin`), built by GitHub Actions from `firmware/`. |

## Releasing

```bash
python tools/bump_version.py patch      # edits FW_VERSION in firmware/agri_rover/config.h
git commit -am "Fix X" && git push
```

CI (`.github/workflows/build-firmware.yml`) runs the tests, compiles, and, because `FW_VERSION` is now newer than
the one in `version.txt`, commits the new `firmware.bin` and `version.txt` here. Each rover then updates itself the
next time it is **stopped and online**, and stays **disarmed** afterwards. Details: [docs/RELEASING.md](../docs/RELEASING.md).

If you change code but do not bump the version, CI still builds and tests it but does **not** publish: rovers only
update to a *newer* version.

(Empty until the first CI run publishes v0.1.0.)
