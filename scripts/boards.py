"""Board manifests (targets/<id>.json): loading and validation.

The directory is not called boards/ because PlatformIO reserves that name
for its own custom board definitions.

Shared by the PlatformIO pre-build script (scripts/board.py), the release
tool (tools/release.py) and the CI check (tools/check_boards.py). Standard
library only, so it runs inside PlatformIO's Python as well.
"""
import configparser
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BOARDS_DIR = ROOT / "targets"

REQUIRED_PINS = ("sda", "scl", "gps_rx", "gps_tx")
REQUIRED_FLASH = ("mode", "freq", "size", "bootloader_offset")


class BoardError(ValueError):
    pass


def validate(board: dict, source: str) -> None:
    for key in ("id", "name", "chip", "status", "pins", "gps_uart", "flash"):
        if key not in board:
            raise BoardError(f"{source}: missing '{key}'")
    for pin in REQUIRED_PINS:
        if not isinstance(board["pins"].get(pin), int):
            raise BoardError(f"{source}: pins.{pin} must be an integer GPIO number")
    if len({board["pins"][p] for p in REQUIRED_PINS}) != len(REQUIRED_PINS):
        raise BoardError(f"{source}: two signals share a GPIO")
    if not isinstance(board["gps_uart"], int) or board["gps_uart"] == 0:
        raise BoardError(f"{source}: gps_uart must be a UART other than 0 (UART0 is the host link)")
    for key in REQUIRED_FLASH:
        if key not in board["flash"]:
            raise BoardError(f"{source}: missing flash.{key}")


def load(board_id: str) -> dict:
    path = BOARDS_DIR / f"{board_id}.json"
    if not path.is_file():
        known = ", ".join(sorted(p.stem for p in BOARDS_DIR.glob("*.json")))
        raise BoardError(f"no manifest targets/{board_id}.json (known boards: {known})")
    board = json.loads(path.read_text())
    validate(board, path.name)
    if board["id"] != board_id:
        raise BoardError(f"{path.name}: id is '{board['id']}', expected '{board_id}'")
    return board


def load_all() -> list[dict]:
    return [load(p.stem) for p in sorted(BOARDS_DIR.glob("*.json"))]


def defines(board: dict) -> dict:
    """C preprocessor definitions that config.h and version.h pick up."""
    pins = board["pins"]
    return {
        "MSNS_BOARD_ID": board["id"],
        "PIN_SDA": pins["sda"],
        "PIN_SCL": pins["scl"],
        "PIN_GPS_RX": pins["gps_rx"],
        "PIN_GPS_TX": pins["gps_tx"],
        "GPS_UART_NUM": board["gps_uart"],
    }


def platformio_envs() -> dict[str, dict]:
    """{env name: its options} from platformio.ini."""
    ini = configparser.ConfigParser()
    ini.read(ROOT / "platformio.ini")
    return {s[len("env:"):]: dict(ini[s]) for s in ini.sections() if s.startswith("env:")}
