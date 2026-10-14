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
"""

import argparse
import hashlib
import json
import pathlib
import shutil
import sys

# File-name suffix per display (athan.yaml display_type): the OLED keeps the original names.
SUFFIX = {"oled": "", "tft": "-tft"}


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
    if data[:1] != b"\xe9":
        print("warning: this does not look like an ESP32 app image (first byte is not 0xE9)", file=sys.stderr)
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
