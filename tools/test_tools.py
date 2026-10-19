"""Tests for the release/log tooling:  python -m unittest tools/test_tools.py  (from the repo root)."""
import hashlib
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bump_version  # noqa: E402
import consolidate_logs  # noqa: E402
import publish_ota  # noqa: E402

HEADER = "utc,uptime_ms,lat,lon,fix,sats,hdop,speed_mps,course_deg,yaw_deg,mode,batt_v,xte_m,wp,event,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,pitch_deg,roll_deg"


def make_repo(version="0.1.0", slot="0x180000"):
    root = Path(tempfile.mkdtemp())
    fw = root / "firmware" / "agri_rover"
    fw.mkdir(parents=True)
    (fw / "config.h").write_text(f'#define FW_VERSION "{version}"\n')
    (fw / "partitions.csv").write_text(f"nvs,data,nvs,0x9000,0x5000,\napp0,app,ota_0,0x10000,{slot},\n")
    return root


def make_bin(root, size=200_000, byte=0x11):
    p = root / "app.bin"
    p.write_bytes(bytes([0xE9]) + bytes([byte]) * (size - 1))
    return p


class PublishOta(unittest.TestCase):
    def test_first_publish_writes_manifest_with_correct_md5_and_size(self):
        root = make_repo()
        b = make_bin(root)
        code, published = publish_ota.run(root, b)
        self.assertEqual((code, published), (0, True))
        m = publish_ota.read_manifest(root / "ota" / "version.txt")
        self.assertEqual(m["version"], "0.1.0")
        self.assertEqual(m["md5"], hashlib.md5(b.read_bytes()).hexdigest())
        self.assertEqual(int(m["size"]), 200_000)
        self.assertEqual((root / "ota" / "firmware.bin").read_bytes(), b.read_bytes())

    def test_changed_code_without_a_version_bump_is_not_published(self):
        root = make_repo()
        publish_ota.run(root, make_bin(root, byte=0x11))
        code, published = publish_ota.run(root, make_bin(root, byte=0x22))
        self.assertEqual((code, published), (0, False))
        self.assertEqual(publish_ota.read_manifest(root / "ota" / "version.txt")["md5"],
                         hashlib.md5(bytes([0xE9]) + bytes([0x11]) * 199_999).hexdigest())

    def test_a_newer_version_is_published_numerically_not_alphabetically(self):
        root = make_repo("0.9.0")
        publish_ota.run(root, make_bin(root, byte=0x11))
        (root / "firmware" / "agri_rover" / "config.h").write_text('#define FW_VERSION "0.10.0"\n')
        code, published = publish_ota.run(root, make_bin(root, byte=0x22))
        self.assertEqual((code, published), (0, True))  # "0.10.0" < "0.9.0" as strings, > as versions
        self.assertEqual(publish_ota.read_manifest(root / "ota" / "version.txt")["version"], "0.10.0")

    def test_going_backwards_is_an_error(self):
        root = make_repo("0.5.0")
        publish_ota.run(root, make_bin(root))
        (root / "firmware" / "agri_rover" / "config.h").write_text('#define FW_VERSION "0.4.0"\n')
        code, published = publish_ota.run(root, make_bin(root, byte=0x33))
        self.assertEqual((code, published), (1, False))

    def test_an_image_that_does_not_fit_an_ota_slot_is_refused(self):
        root = make_repo(slot="0x20000")  # 128 KB slot
        code, published = publish_ota.run(root, make_bin(root, size=200_000))
        self.assertEqual((code, published), (1, False))
        self.assertFalse((root / "ota").exists())

    def test_not_an_application_image_is_refused(self):
        root = make_repo()
        p = root / "x.bin"
        p.write_bytes(b"\x00" * 200_000)
        self.assertEqual(publish_ota.run(root, p), (1, False))
        tiny = root / "t.bin"
        tiny.write_bytes(b"\xE9" * 100)
        self.assertEqual(publish_ota.run(root, tiny), (1, False))

    def test_dry_run_writes_nothing(self):
        root = make_repo()
        self.assertEqual(publish_ota.run(root, make_bin(root), dry_run=True), (0, False))
        self.assertFalse((root / "ota").exists())

    def test_published_manifest_is_accepted_by_the_firmware_parser_rules(self):
        # Mirror of ota_github.cpp: version x.y.z, 32 hex md5, size between 64 KB and 16 MB.
        root = make_repo()
        publish_ota.run(root, make_bin(root))
        m = publish_ota.read_manifest(root / "ota" / "version.txt")
        publish_ota.parse_semver(m["version"])
        self.assertRegex(m["md5"], r"^[0-9a-f]{32}$")
        self.assertTrue(64 * 1024 <= int(m["size"]) <= 16 * 1024 * 1024)


class BumpVersion(unittest.TestCase):
    def run_bump(self, start, how):
        d = Path(tempfile.mkdtemp()) / "config.h"
        d.write_text(f'// header\n#define FW_VERSION "{start}"\n#define OTHER "9.9.9"\n')
        code = bump_version.main([how], config=d)
        return code, d.read_text()

    def test_patch_minor_major_and_explicit(self):
        self.assertIn('"0.1.1"', self.run_bump("0.1.0", "patch")[1])
        self.assertIn('"0.2.0"', self.run_bump("0.1.7", "minor")[1])
        self.assertIn('"1.0.0"', self.run_bump("0.4.2", "major")[1])
        self.assertIn('"2.5.1"', self.run_bump("0.1.0", "2.5.1")[1])

    def test_only_fw_version_changes(self):
        code, text = self.run_bump("0.1.0", "patch")
        self.assertEqual(code, 0)
        self.assertIn('#define OTHER "9.9.9"', text)
        self.assertIn("// header", text)

    def test_garbage_is_rejected_and_file_untouched(self):
        code, text = self.run_bump("0.1.0", "banana")
        self.assertEqual(code, 1)
        self.assertIn('"0.1.0"', text)


class ConsolidateLogs(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp()) / "logs"

    def chunk(self, rover, day, name, rows, header=HEADER):
        d = self.root / rover / day
        d.mkdir(parents=True, exist_ok=True)
        (d / name).write_text(header + "\n" + "\n".join(rows) + "\n")

    def test_chunks_become_one_day_file_in_order_with_one_header(self):
        self.chunk("rover-A", "2026-10-01", "000001_001.csv", ["b1", "b2"])
        self.chunk("rover-A", "2026-10-01", "000001_000.csv", ["a1", "a2"])
        self.chunk("rover-A", "2026-10-01", "000002_000.csv", ["c1"])
        merged, problems = consolidate_logs.consolidate(self.root, "2026-10-05")
        self.assertEqual(problems, [])
        text = (self.root / "rover-A" / "2026-10-01.csv").read_text().splitlines()
        self.assertEqual(text, [HEADER, "a1", "a2", "b1", "b2", "c1"])
        self.assertFalse((self.root / "rover-A" / "2026-10-01").exists())  # chunks removed

    def test_today_is_left_alone_because_rovers_may_still_be_uploading(self):
        self.chunk("rover-A", "2026-10-05", "000001_000.csv", ["x"])
        merged, _ = consolidate_logs.consolidate(self.root, "2026-10-05")
        self.assertEqual(merged, [])
        self.assertTrue((self.root / "rover-A" / "2026-10-05" / "000001_000.csv").exists())

    def test_late_chunks_are_merged_into_the_existing_day_file(self):
        self.chunk("rover-A", "2026-10-01", "000001_000.csv", ["a1"])
        consolidate_logs.consolidate(self.root, "2026-10-05")
        self.chunk("rover-A", "2026-10-01", "000009_000.csv", ["late1"])  # an offline rover catching up
        consolidate_logs.consolidate(self.root, "2026-10-06")
        text = (self.root / "rover-A" / "2026-10-01.csv").read_text().splitlines()
        self.assertEqual(text, [HEADER, "a1", "late1"])

    def test_a_chunk_with_a_different_header_is_not_mixed_in(self):
        self.chunk("rover-A", "2026-10-01", "000001_000.csv", ["a1"])
        self.chunk("rover-A", "2026-10-01", "000002_000.csv", ["z"], header="other,columns")
        merged, problems = consolidate_logs.consolidate(self.root, "2026-10-05")
        self.assertEqual(len(problems), 1)
        self.assertEqual((self.root / "rover-A" / "2026-10-01.csv").read_text().splitlines(), [HEADER, "a1"])
        self.assertTrue((self.root / "rover-A" / "2026-10-01" / "000002_000.csv").exists())

    def test_rovers_are_kept_separate_and_unsynced_is_merged_too(self):
        self.chunk("rover-A", "2026-10-01", "000001_000.csv", ["a"])
        self.chunk("rover-B", "2026-10-01", "000001_000.csv", ["b"])
        self.chunk("rover-A", "unsynced", "000003_000.csv", ["u"])
        merged, _ = consolidate_logs.consolidate(self.root, "2026-10-05")
        self.assertEqual(len(merged), 3)
        self.assertEqual((self.root / "rover-B" / "2026-10-01.csv").read_text().splitlines()[1], "b")
        self.assertTrue((self.root / "rover-A" / "unsynced.csv").exists())

    def test_missing_logs_dir_is_fine(self):
        self.assertEqual(consolidate_logs.consolidate(self.root / "nope", "2026-10-05"), ([], []))


class PinChecks(unittest.TestCase):
    """The compile-time pin checks in config.h must catch real wiring mistakes."""

    CONFIG = Path(__file__).resolve().parent.parent / "firmware" / "agri_rover" / "config.h"

    def compiles(self, mutate=None):
        import shutil
        import subprocess
        gpp = shutil.which("g++")
        if not gpp:
            self.skipTest("g++ not available")
        text = self.CONFIG.read_text(encoding="utf-8")
        if mutate:
            mutated = mutate(text)
            self.assertNotEqual(mutated, text, "mutation did not change config.h")
            text = mutated
        d = Path(tempfile.mkdtemp())
        (d / "config.h").write_text(text, encoding="utf-8")
        (d / "t.cpp").write_text('#include "config.h"\nint main(){return 0;}\n')
        p = subprocess.run([gpp, "-std=c++17", "-fsyntax-only", str(d / "t.cpp")], capture_output=True, text=True)
        return p.returncode == 0, p.stderr

    def test_shipped_pin_map_is_valid(self):
        ok, err = self.compiles()
        self.assertTrue(ok, err)

    def test_the_original_sketch_clash_is_rejected(self):
        # The Oct-10 sketch used GPIO 21 for BR_EN, which is also the IMU's I2C SDA.
        ok, err = self.compiles(lambda t: t.replace("{13, 23, 4, 16}", "{13, 23, 4, 21}"))
        self.assertFalse(ok)
        self.assertIn("share one GPIO", err)

    def test_a_flash_pin_or_boot_pin_for_enable_is_rejected(self):
        ok, _ = self.compiles(lambda t: t.replace("{13, 23, 4, 16}", "{13, 23, 4, 9}"))   # flash pin
        self.assertFalse(ok)
        ok, _ = self.compiles(lambda t: t.replace("{13, 23, 4, 16}", "{13, 23, 4, 12}"))  # boot-strap pin
        self.assertFalse(ok)
        ok, _ = self.compiles(lambda t: t.replace("{13, 23, 4, 16}", "{13, 23, 4, 36}"))  # input-only
        self.assertFalse(ok)


if __name__ == "__main__":
    unittest.main()
