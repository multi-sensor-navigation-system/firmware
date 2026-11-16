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

CHIPS_DIR = ROOT / "chips"

REQUIRED_PINS = ("sda", "scl", "gps_rx", "gps_tx")
# Signals the chip drives. gps_rx only listens, so an input-only GPIO is fine.
OUTPUT_PINS = ("sda", "scl", "gps_tx")
REQUIRED_FLASH = ("mode", "freq", "size", "bootloader_offset")


class BoardError(ValueError):
    pass


def load_chip(chip: str) -> dict:
    """Pin rules for one chip family (chips/<chip>.json)."""
    path = CHIPS_DIR / f"{chip}.json"
    if not path.is_file():
        known = ", ".join(sorted(p.stem for p in CHIPS_DIR.glob("*.json")))
        raise BoardError(f"no pin rules chips/{chip}.json (known chips: {known})")
    return json.loads(path.read_text())


def pin_errors(pins: dict, gps_uart: int, chip: dict, reserved=None) -> list[str]:
    """Every reason this pin assignment cannot work on this chip and board.

    An empty list means the assignment is allowed. The same function backs the
    manifest check and any tool that lets a user choose pins.
    """
    errors = []
    reserved = reserved or {}
    for signal in REQUIRED_PINS:
        gpio = pins.get(signal)
        if not isinstance(gpio, int) or isinstance(gpio, bool):
            errors.append(f"{signal}: must be an integer GPIO number")
            continue
        if gpio not in chip["gpio"]:
            errors.append(f"{signal}: GPIO {gpio} does not exist on the {chip['chip']}")
        elif str(gpio) in chip["blocked"]:
            errors.append(f"{signal}: GPIO {gpio} is {chip['blocked'][str(gpio)]}")
        elif str(gpio) in reserved:
            errors.append(f"{signal}: GPIO {gpio} is reserved on this board: {reserved[str(gpio)]}")
        elif signal in OUTPUT_PINS and gpio in chip["input_only"]:
            errors.append(f"{signal}: GPIO {gpio} is input-only")
    used = [pins[s] for s in REQUIRED_PINS if isinstance(pins.get(s), int)]
    for gpio in sorted({g for g in used if used.count(g) > 1}):
        shared = ", ".join(s for s in REQUIRED_PINS if pins.get(s) == gpio)
        errors.append(f"GPIO {gpio} is assigned to more than one signal ({shared})")
    if gps_uart not in chip["uarts"]:
        allowed = ", ".join(str(u) for u in chip["uarts"])
        errors.append(f"gps_uart: UART{gps_uart} cannot be used (allowed: {allowed}; UART0 is the host link)")
    return errors


def validate(board: dict, source: str) -> None:
    for key in ("id", "name", "chip", "status", "pins", "gps_uart", "flash"):
        if key not in board:
            raise BoardError(f"{source}: missing '{key}'")
    errors = pin_errors(board["pins"], board["gps_uart"], load_chip(board["chip"]), board.get("reserved"))
    if errors:
        raise BoardError(f"{source}: " + "; ".join(errors))
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
