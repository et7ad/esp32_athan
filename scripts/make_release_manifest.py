#!/usr/bin/env python3
"""Write manifest.json (or manifest-tft.json) for a firmware GitHub Release.

Devices check https://github.com/et7ad/esp32_athan/releases/latest/download/manifest.json (the `update:` entity in
firmware/athan.yaml, every 6 hours and on "Check For Update"); clocks built for the TFT display check
manifest-tft.json in the same release. The manifest names the OTA image, its MD5 and the version; a relative path is
resolved against the manifest's own URL, so both files must be assets of the SAME release.

Usage:
    python3 scripts/make_release_manifest.py path/to/firmware.ota.bin --version 3.0.1 [--summary "What changed"]
                                             [--display oled|tft]

Writes, next to the input file:
    athan-v3.ota.bin   (a copy, the asset name the manifest points to; TFT: athan-v3-tft.ota.bin)
    manifest.json      (TFT: manifest-tft.json)

Upload both to a new GitHub Release (tag e.g. v3.0.1) and mark it "latest". A release that serves both displays
carries all four files, both built from the same version. The version must equal
`project_version` in firmware/athan.yaml at build time, or the devices keep offering the update.

The image is checked first, and nothing is written when a check fails: it must be an ESP32-S3 app image (the OTA
image, not the factory image a USB flash takes), built for --display (each build checks its own manifest, and its
URL is inside the image), and the version must appear in it.
"""

import argparse
import hashlib
import json
import pathlib
import re
import shutil
import sys

# File-name suffix per display (athan.yaml display_type): the OLED keeps the original names.
SUFFIX = {"oled": "", "tft": "-tft"}
ESP32S3_CHIP_ID = 9  # esp_chip_id_t in the image header (bytes 12-13), as flash_firmware.sh checks it


def image_problem(data: bytes, display: str, version: str) -> str:
    """Why this image must not be released as the --display build of --version, or ""."""
    if len(data) < 24 or data[0] != 0xE9:
        return "not an ESP32 app image (the first byte is not 0xE9)"
    if len(data) > 0x10000 and data[0x8000:0x8002] == b"\xaa\x50":
        return ("a factory image (bootloader, partition table and app, for a USB flash from 0x0). Release the OTA "
                "image: firmware.ota.bin, or the dashboard's OTA format")
    chip = int.from_bytes(data[12:14], "little")
    if chip != ESP32S3_CHIP_ID:
        return f"built for another chip (chip id {chip}; the ESP32-S3 is {ESP32S3_CHIP_ID})"
    # The build's update URL (athan.yaml update_manifest: .../releases/latest/download/manifest<suffix>.json)
    checks = {m.decode() for m in re.findall(rb"/releases/latest/download/(manifest[A-Za-z0-9_-]*)\.json", data)}
    want = f"manifest{SUFFIX[display]}"
    if checks != {want}:
        built = {f"manifest{s}": d for d, s in SUFFIX.items()}
        found = ", ".join(f"{c}.json ({built[c]} build)" if c in built else f"{c}.json" for c in sorted(checks))
        hint = f": use --display {built[next(iter(checks))]}" if len(checks) == 1 and next(iter(checks)) in built else ""
        return (f"this image updates from {found or 'no release manifest'}, but --display {display} writes "
                f"{want}.json{hint}")
    if not re.search(rb"(?<![0-9.])" + re.escape(version.encode()) + rb"(?![0-9.])", data):
        return (f"version {version} does not appear in the image: build it with project_version \"{version}\" in "
                "firmware/athan.yaml, or pass the version it was built with")
    return ""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ota_bin", type=pathlib.Path, help="the .ota.bin ESPHome built (not the factory image)")
    ap.add_argument("--version", required=True, help="same as project_version in athan.yaml, e.g. 3.0.1")
    ap.add_argument("--summary", default="", help="one line shown in Home Assistant")
    ap.add_argument("--release-url", default="", help="link to the release notes")
    ap.add_argument("--display", choices=sorted(SUFFIX), default="oled", help="the display the image was built for")
    a = ap.parse_args()
    sfx = SUFFIX[a.display]
    asset_name = f"athan-v3{sfx}.ota.bin"
    manifest_name = f"manifest{sfx}.json"

    if not a.ota_bin.is_file():
        print(f"not found: {a.ota_bin}", file=sys.stderr)
        return 1
    data = a.ota_bin.read_bytes()
    problem = image_problem(data, a.display, a.version)
    if problem:
        print(f"✗ {a.ota_bin}: {problem}. Nothing written.", file=sys.stderr)
        return 1
    out_dir = a.ota_bin.parent
    asset = out_dir / asset_name
    if asset.resolve() != a.ota_bin.resolve():
        shutil.copyfile(a.ota_bin, asset)
    ota = {"path": asset_name, "md5": hashlib.md5(data).hexdigest()}
    if a.summary:
        ota["summary"] = a.summary
    if a.release_url:
        ota["release_url"] = a.release_url
    manifest = {
        "name": "Athan clock" + (f" ({a.display.upper()})" if sfx else ""),
        "version": a.version,
        "home_assistant_domain": "esphome",
        "builds": [{"chipFamily": "ESP32-S3", "ota": ota}],
    }
    (out_dir / manifest_name).write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"✓ {out_dir / manifest_name}  ({len(data) / 1e6:.2f} MB image, md5 {ota['md5']})")
    print(f"  upload {asset.name} and {manifest_name} to the GitHub Release v{a.version}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
