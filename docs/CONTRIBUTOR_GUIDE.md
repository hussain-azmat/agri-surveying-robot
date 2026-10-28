# Contributor guide: from a blank laptop to a flashed rover, then making changes

Written for a new team member on **Windows 10/11**. (On macOS/Linux the steps are the same; install the same tools with your
package manager, and the serial port is `/dev/ttyUSB0` or `/dev/cu.usbserial-*` instead of `COM5`.)

Time: about 45 minutes the first time, most of it downloads.

> **Safety first.** The first time you power the rover on a new build, **put it on a stand with the wheels off the ground**.
> Keep a hand on the big red STOP button on the control page. Never commit WiFi passwords, GitHub tokens or access codes.

---

## Part 1. Accounts and tools (once per laptop)

### 1.1 GitHub account and the invitation
1. Create a free account at https://github.com/signup (or use your existing one). Remember your **username**: give it to
   the repo owner (Azmat) so they can invite you. If they already did:
2. Open the invitation email from GitHub, or go to https://github.com/hussain-azmat/agri-surveying-robot/invitations ,
   and click **Accept invitation**. You now have write access.
3. Check: you can open https://github.com/hussain-azmat/agri-surveying-robot while signed in, and your first push
   (Part 3) goes through. If a push says "permission denied", the invitation was not accepted yet.

### 1.2 Install these four things
| Tool | Where | Notes |
|---|---|---|
| **Git for Windows** | https://git-scm.com/download/win | Accept the defaults. It includes *Git Credential Manager*, which handles your GitHub login. |
| **Python 3.12** | https://www.python.org/downloads/ | Tick **"Add python.exe to PATH"** on the first installer screen. |
| **Arduino IDE 2** | https://www.arduino.cc/en/software | Used to compile and flash. |
| **USB-serial driver** | only if the board does not show up as a COM port | Most ESP32 DevKit boards use a *CP210x* (Silicon Labs) or *CH340* chip; look at the small chip next to the USB socket. |

Restart the laptop (or at least open a new terminal) after installing, so `git` and `python` are found.

### 1.3 Tell Git who you are
Open **PowerShell** (Start menu → PowerShell) and run, with your own details:

```powershell
git config --global user.name "Your Name"
git config --global user.email "you@example.com"
```

### 1.4 Get the code
```powershell
cd $HOME\Documents
git clone https://github.com/hussain-azmat/agri-surveying-robot.git
cd agri-surveying-robot
```
The repo is public, so cloning needs no login. **Logging in happens the first time you push** (Part 3); a browser window
opens, you sign in to GitHub and approve. You do not create or paste any token yourself.

Folder map (details in [README.md](../README.md) and [Software/Readme.txt](../Software/Readme.txt)): the code is in `Software/`,
**one folder per function** (MPU, GPS, Motors, WiFi, OTA, Logging, Path, Remote, Safety, Common, Rover). Each has a `Readme.txt`,
`Src/` (the code), `Bin/` (a compiled test image) and `Test/` (laptop unit tests and the on-rover checklist). Pins and limits are in
`Software/Common/Src/config.h`.

---

## Part 2. Compile and flash the rover for the first time

The very first flash must be over USB, because it writes the partition table that makes later over-the-air (OTA) updates possible.
There are two ways. **Way A is the quickest** (nothing to compile); **Way B** is what you will use for development.

### Way A. Flash the ready-made image (no Arduino setup needed)
Every push builds the firmware on GitHub and keeps the images for 30 days.
1. Go to https://github.com/hussain-azmat/agri-surveying-robot/actions → open the newest green **firmware** run →
   scroll to **Artifacts** → download `firmware-<hash>` (needs you to be signed in to GitHub) → unzip it.
   You want `agri_rover.ino.merged.bin` (4 MB: bootloader + partition table + app, flashed at address 0).
2. Plug the ESP32 in with a **data** USB cable (some cables are charge-only). Find the port: Device Manager → *Ports (COM & LPT)*,
   e.g. `COM5`.
3. In PowerShell:
   ```powershell
   python -m pip install esptool
   python -m esptool --chip esp32 --port COM5 erase_flash
   python -m esptool --chip esp32 --port COM5 write_flash 0x0 "$HOME\Downloads\agri_rover.ino.merged.bin"
   ```
   If it stops at `Connecting....`, **hold the BOOT button** on the board until it starts writing, then release.
4. Skip to **2.4 Check it works**.

### Way B. Compile and flash from the Arduino IDE
**2.1 One-time IDE setup**
1. Open Arduino IDE → *File → Preferences* → in **Additional boards manager URLs** paste
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json` → OK.
2. *Tools → Board → Boards Manager* → search **esp32** → install **"esp32 by Espressif Systems" version 3.1.3**
   (use exactly 3.1.3; the firmware is tested against it).
3. *Tools → Manage Libraries* → search **ArduinoJson** (by Benoit Blanchon) → install version **7.4.2** (any 7.x works).

**2.2 Gather the sketch, open and configure**
The Arduino IDE needs one folder per sketch, so first gather the per-function folders into one. In PowerShell, in the repo folder:
```powershell
python tools\stage_sketch.py
```
This creates `build\stage\agri_rover\` (a copy; **edit the files in `Software\`, never this copy**; it is rebuilt each time).
1. *File → Open* → `agri-surveying-robot\build\stage\agri_rover\agri_rover.ino`. All the other files open as tabs.
2. *Tools → Board → esp32 → **ESP32 Dev Module***.
3. *Tools → Port* → the COM port of your board.
4. Leave the other Tools options at their defaults. The folder's own `partitions.csv` (two 1.5 MB OTA slots) is used automatically.

**2.3 Upload**
Click the **Upload** arrow (→). Wait for `Hard resetting via RTS pin...`. (If it stays on `Connecting.....`, hold BOOT.)
The compile prints "Sketch uses ... bytes (83%)".

### 2.4 Check it works
1. *Tools → Serial Monitor*, set the speed to **115200**, press the board's **EN/RST** button. You should see lines of JSON, for example:
   - `{"status":"INFO","message":"agri rover firmware v0.4.0 starting"}`
   - the rover's id and **MAC address** (write it down)
   - the IMU pin check: `... answers on SDA=GPIO21 SCL=GPIO22` is good. A warning about *not found / swapped* means the IMU wires need
     checking: see "Confirm the IMU pins" in [PINOUT.md](PINOUT.md).
   - `{"status":"READY","state":"DISARMED"}`
2. The rover starts its own WiFi network **`RoverSetup-xxxx`**. Its password is the rover's access code. Get it with:
   ```powershell
   python tools\ap_password.py <the MAC from the serial output>
   ```
3. Join `RoverSetup-xxxx` from a phone or laptop. Any web page opens the setup page. Pick the WiFi, enter its password
   (the rover tests the connection first and only saves it if it works).
4. The GitHub token field on that page is **optional**. The repo is public, so updates need no token. A token is only for the
   rover to upload its logs and recorded paths (ask Azmat before creating one; never share it or commit it).
5. Open the control page (`http://rover-xxxxxx.local` or the IP shown). Continue with [BRINGUP.md](BRINGUP.md), stage by stage,
   **wheels off the ground** until it says otherwise.

### Troubleshooting
| Problem | Fix |
|---|---|
| No COM port appears | Different USB cable (data, not charge-only); install the CP210x/CH340 driver; try another USB port. |
| `Failed to connect` / stuck on `Connecting....` | Hold the BOOT button while uploading; close the Serial Monitor (it holds the port). |
| `Sketch too big` | Use the exact versions above; do not change the partition scheme. |
| IMU "not found" | Wiring or pins; see PINOUT.md. |
| Board keeps rebooting | Weak USB power: use a good cable/port, or power the rover from its battery. |

---

## Part 3. Making changes (daily workflow)

### 3.1 The rules of the road
* `main` is the version the rovers install. **Anything that bumps `FW_VERSION` and lands on `main` is installed by every rover
  that is online and stopped, automatically.** So:
  * Work on a **branch**, open a **pull request**, let GitHub's checks pass, and merge only once the change has been tested on a rover.
  * Only bump the version in the change you actually want released.
* There is **no automatic rollback**. Test on one rover first; to undo a bad release, publish a *newer* version that reverts it.
* The checks on every push or PR: host unit tests, Python tool tests, a full ESP32 compile. If they are red, do not merge.

### 3.2 Step by step
```powershell
cd $HOME\Documents\agri-surveying-robot
git checkout main
git pull                                    # always start from the latest
git checkout -b my-change                   # your branch, any name
```
1. **Edit** the code in `Software/<Function>/Src/` (any editor, for example VS Code). Pins and limits live in
   `Software/Common/Src/config.h`; one job per folder.
2. **Compile**: run `python tools\stage_sketch.py` again to refresh `build\stage\agri_rover\`, then press the ✓ button in the
   IDE on that sketch. (The laptop unit tests need a C++ compiler; skip them, CI runs them for you.)
3. **Test on the rover by USB**: IDE Upload as in Way B. This does not need a version bump. To test ONE function on its own,
   flash its bench (`Software/<Function>/Bin/<name>.bin`, or `python tools\stage_sketch.py mpu` and open
   `build\stage\mpu\mpu.ino`); each function's `Readme.txt` and `Test\ON_ROVER_TEST.txt` say what to look for.
4. **Commit and push your branch:**
   ```powershell
   git add -A
   git commit -m "Short description of what changed and why"
   git push -u origin my-change
   ```
   The first push opens a browser to sign in to GitHub (Git Credential Manager). Sign in once, approve, done.
5. On GitHub, click **Compare & pull request**. Wait for the green check.
6. **To release it to the rovers** (only after you tested it): bump the version, commit and push to the PR, then merge.
   ```powershell
   python tools\bump_version.py patch      # 0.4.0 -> 0.4.1   (minor / major also work)
   git add -A
   git commit -m "Release 0.3.1"
   git push
   ```
   After merging, the *firmware* workflow compiles the image and publishes `ota/firmware.bin` + `ota/version.txt`
   (version, MD5, size) to `main` by itself. Rovers pick it up at their next power-up check, or when you press *Check for update*.
   Full details: [RELEASING.md](RELEASING.md).
7. After merging, update your copy: `git checkout main` then `git pull`. (The workflow adds its own commit to `main`, so always
   `git pull` before starting new work. If a push is rejected with "fetch first", run `git pull --rebase` and push again.)

### 3.3 Things that commonly go wrong
| Problem | Fix |
|---|---|
| Two people bumped the version, merge conflict in `config.h` | Keep the **higher** version number (CI refuses to publish a version older than the current one). |
| Pushed code but no new version appeared in `ota/` | The version was not bumped, or the checks failed. Open the *Actions* tab. |
| Rover did not update | It updates only when **disarmed and online**; check the control page → System → *Check for update*. |
| Update published by mistake | Publish a newer version that reverts it. |
| Git asks for a password in the terminal | Use the browser sign-in that Git Credential Manager offers; GitHub no longer accepts account passwords for git. |

### 3.4 Where to read more
[README.md](../README.md) (overview, serial output, log format) · [ARCHITECTURE.md](ARCHITECTURE.md) (how the modules fit, the safety design) ·
[PINOUT.md](PINOUT.md) (wiring) · [BRINGUP.md](BRINGUP.md) (staged tests on the real rover) · [RELEASING.md](RELEASING.md) (OTA releases).
