#!/usr/bin/env python3
"""Host test for the pin rules (chips/<chip>.json and boards.pin_errors)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
import boards  # noqa: E402

ESP32 = boards.load_chip("esp32")
S3 = boards.load_chip("esp32s3")
GOOD = {"sda": 21, "scl": 22, "gps_rx": 16, "gps_tx": 17}


def errs(chip, uart=2, reserved=None, **change):
    return boards.pin_errors({**GOOD, **change}, uart, chip, reserved)


def expect(errors, fragment):
    assert any(fragment in e for e in errors), f"expected {fragment!r} in {errors}"


def main() -> int:
    # Rule files are internally consistent.
    for chip in (ESP32, S3):
        gpio = set(chip["gpio"])
        assert {int(g) for g in chip["blocked"]} <= gpio, chip["chip"]
        assert set(chip["input_only"]) <= gpio, chip["chip"]
        assert 0 not in chip["uarts"], chip["chip"]

    assert errs(ESP32) == []
    expect(errs(ESP32, sda=6), "SPI flash")
    expect(errs(ESP32, scl=12), "strapping")
    expect(errs(ESP32, gps_tx=1), "host link")
    expect(errs(ESP32, sda=34), "input-only")
    assert errs(ESP32, gps_rx=34) == []          # a receive pin may be input-only
    expect(errs(ESP32, sda=24), "does not exist")
    expect(errs(ESP32, scl=21), "more than one signal")
    expect(errs(ESP32, uart=0), "UART0")
    expect(errs(ESP32, sda=True), "integer")
    assert len(errs(ESP32, sda=6, scl=12)) == 2  # every problem is reported, not just the first

    s3_good = {"sda": 8, "scl": 9, "gps_rx": 18, "gps_tx": 17}
    assert boards.pin_errors(s3_good, 1, S3) == []
    expect(boards.pin_errors({**s3_good, "sda": 19}, 1, S3), "USB")
    expect(boards.pin_errors({**s3_good, "sda": 22}, 1, S3), "does not exist")
    expect(boards.pin_errors({**s3_good, "sda": 35}, 1, S3, {"35": "octal PSRAM"}), "reserved on this board")

    # Every shipped manifest passes its own chip's rules.
    for board in boards.load_all():
        chip = boards.load_chip(board["chip"])
        assert boards.pin_errors(board["pins"], board["gps_uart"], chip, board.get("reserved")) == [], board["id"]

    # The masks compiled into the firmware allow exactly what pin_errors allows.
    for board in boards.load_all():
        chip = boards.load_chip(board["chip"])
        reserved = board.get("reserved")
        out_mask, in_mask = boards.pin_masks(chip, reserved)
        assert out_mask & ~in_mask == 0, board["id"]
        free = [g for g in range(64) if (out_mask >> g) & 1]
        for gpio in range(64):
            a, b, c = [g for g in free if g != gpio][:3]  # three other pins known to be fine
            as_output = {"sda": gpio, "scl": a, "gps_rx": b, "gps_tx": c}
            as_input = {"sda": a, "scl": b, "gps_rx": gpio, "gps_tx": c}
            uart = board["gps_uart"]
            assert (boards.pin_errors(as_output, uart, chip, reserved) == []) == bool((out_mask >> gpio) & 1), (board["id"], gpio)
            assert (boards.pin_errors(as_input, uart, chip, reserved) == []) == bool((in_mask >> gpio) & 1), (board["id"], gpio)

    # config.h's fallback masks (builds made outside PlatformIO) are the esp32dev ones.
    config = (boards.ROOT / "include" / "config.h").read_text()
    esp32dev = boards.defines(boards.load("esp32dev"))
    for name in ("MSNS_PIN_MASK_OUT", "MSNS_PIN_MASK_IN"):
        assert f"#define {name} {esp32dev[name]}" in config, name

    rules = boards.pin_rules(boards.load("esp32-s3-devkitc-1"))
    assert "35" in rules["blocked"] and "19" in rules["blocked"]

    print("pin rules OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
