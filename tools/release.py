#!/usr/bin/env python3
"""Assemble release artifacts from a finished PlatformIO build.

For every board manifest this merges bootloader + partition table + boot_app0
+ application into one image flashable at 0x0, then writes manifest.json —
the file `nav flash` and the web flasher read to find the right image.

    pio run                                  # build every environment first
    python tools/release.py --tag v0.2.0 --out dist
    python tools/release.py --tag v0.2.0 --out dist --site site   # also build the web flasher

Requires esptool >= 5 on PATH.
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
import boards  # noqa: E402

DEFAULT_BOOT_APP0 = (Path.home() / ".platformio" / "packages" / "framework-arduinoespressif32"
                     / "tools" / "partitions" / "boot_app0.bin")


def macro(header: str, name: str) -> str:
    text = (ROOT / "include" / header).read_text()
    m = re.search(rf'^#define\s+{name}\s+"?([^"\s]+)"?', text, re.M)
    if not m:
        raise SystemExit(f"{name} not found in include/{header}")
    return m.group(1)


def git_sha() -> str:
    try:
        return subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT,
                                       stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return "unknown"


def merge(board: dict, build_dir: Path, boot_app0: Path, out: Path) -> None:
    flash = board["flash"]
    parts = [
        (flash["bootloader_offset"], build_dir / "bootloader.bin"),
        ("0x8000", build_dir / "partitions.bin"),
        ("0xe000", boot_app0),
        ("0x10000", build_dir / "firmware.bin"),
    ]
    for _, path in parts:
        if not path.is_file():
            raise SystemExit(f"missing build output: {path} (run `pio run -e {board['id']}` first)")
    cmd = ["esptool", "--chip", board["chip"], "merge-bin", "-o", str(out),
           "--flash-mode", flash["mode"], "--flash-freq", flash["freq"], "--flash-size", flash["size"]]
    for offset, path in parts:
        cmd += [offset, str(path)]
    subprocess.run(cmd, check=True)


# esptool chip id → ESP Web Tools chipFamily
CHIP_FAMILY = {"esp32": "ESP32", "esp32s2": "ESP32-S2", "esp32s3": "ESP32-S3",
               "esp32c3": "ESP32-C3", "esp32c6": "ESP32-C6", "esp32h2": "ESP32-H2"}


def build_site(manifest: dict, dist: Path, site: Path) -> None:
    """The web flasher: web/index.html, the images, and one ESP Web Tools manifest per board.

    The images are copied next to the page because browsers cannot fetch
    GitHub release assets cross-origin.
    """
    (site / "web").mkdir(parents=True, exist_ok=True)
    shutil.copy(ROOT / "web" / "index.html", site / "index.html")
    shutil.copy(dist / "manifest.json", site / "manifest.json")
    for board_id, entry in manifest["boards"].items():
        shutil.copy(dist / entry["image"], site / entry["image"])
        web_manifest = {
            "name": f"MSNS firmware — {entry['name']}",
            "version": manifest["version"],
            "new_install_prompt_erase": False,
            "builds": [{
                "chipFamily": CHIP_FAMILY[entry["chip"]],
                "parts": [{"path": f"../{entry['image']}", "offset": 0}],
            }],
        }
        (site / "web" / f"{board_id}.json").write_text(json.dumps(web_manifest, indent=2) + "\n")
    print(f"  web flasher → {site}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tag", required=True, help="release tag, e.g. v0.2.0")
    ap.add_argument("--out", type=Path, default=ROOT / "dist")
    ap.add_argument("--build-root", type=Path, default=ROOT / ".pio" / "build")
    ap.add_argument("--boot-app0", type=Path, default=DEFAULT_BOOT_APP0)
    ap.add_argument("--site", type=Path, help="also build the web flasher into this directory")
    args = ap.parse_args()

    fw_version = macro("version.h", "MSNS_FW_VERSION")
    if args.tag != f"v{fw_version}":
        raise SystemExit(f"tag {args.tag} does not match MSNS_FW_VERSION {fw_version} in include/version.h")

    args.out.mkdir(parents=True, exist_ok=True)
    manifest = {
        "schema": 1,
        "version": args.tag,
        "git": git_sha(),
        "proto": int(macro("wire.h", "WIRE_PROTO_VERSION")),
        "boards": {},
    }
    for board in boards.load_all():
        image = args.out / f"msns-firmware-{board['id']}-{args.tag}.bin"
        merge(board, args.build_root / board["id"], args.boot_app0, image)
        manifest["boards"][board["id"]] = {
            "name": board["name"],
            "chip": board["chip"],
            "status": board["status"],
            "image": image.name,
            "sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
            "size": image.stat().st_size,
            "offset": "0x0",
        }
        print(f"  {image.name}  {manifest['boards'][board['id']]['sha256'][:16]}…")

    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"  manifest.json  ({len(manifest['boards'])} boards, proto {manifest['proto']})")
    if args.site:
        build_site(manifest, args.out, args.site)
    return 0


if __name__ == "__main__":
    sys.exit(main())
