# Boards

Each supported board is described by one manifest, `targets/<id>.json`, and
one PlatformIO environment with the same name. The manifest is the single
source for the pins, the flash layout and the board's status; the build, the
release images, `nav flash` and the firmware's `HELLO` frame all read from it.

| Board id | Board | Chip | Status |
|---|---|---|---|
| `esp32dev` | ESP32 DevKit (ESP32-WROOM-32) | ESP32 | tested |
| `esp32-s3-devkitc-1` | ESP32-S3 DevKitC-1 | ESP32-S3 | builds in CI, not yet run on hardware |

## Manifest

```json
{
  "id": "esp32dev",
  "name": "ESP32 DevKit (ESP32-WROOM-32)",
  "chip": "esp32",
  "status": "tested",
  "pins": { "sda": 21, "scl": 22, "gps_rx": 16, "gps_tx": 17 },
  "gps_uart": 2,
  "flash": { "mode": "dio", "freq": "40m", "size": "4MB", "bootloader_offset": "0x1000" }
}
```

| Field | Meaning |
|---|---|
| `id` | Board id. Must equal the file name and the PlatformIO environment name |
| `name` | Human-readable name shown by `nav flash` and the web flasher |
| `chip` | esptool chip id (`esp32`, `esp32s3`, …). `nav flash` matches it against the detected chip |
| `status` | Free text. Say honestly whether the board has been run on hardware |
| `pins` | GPIO numbers for the shared I²C bus and the GPS UART |
| `gps_uart` | Hardware UART used for the GPS. UART0 is the host link and cannot be used |
| `flash` | Parameters for merging the release image. `bootloader_offset` is `0x1000` on the ESP32 and `0x0` on the ESP32-S3/C3 |

## Adding a board

1. Create `targets/<id>.json`.
2. Add an environment to `platformio.ini`:
   ```ini
   [env:<id>]
   board        = <PlatformIO board name>
   custom_board = <id>
   ```
3. Run `python3 tools/check_boards.py`, then `pio run -e <id>`.
4. Add the board to the table above.

CI builds every environment, and the release workflow publishes an image for
every manifest automatically.

## Constraints

The firmware pins its sensor task to core 1 and its serial task to core 0, so
it needs a **dual-core** chip (ESP32, ESP32-S3). Single-core parts such as the
ESP32-C3 need a different task layout and are not supported yet.

All boards use the same sensors (MPU-6050, QMC5883L, BMP280, NEO-6M) on one
I²C bus and one UART. Sensor variants are a separate piece of work.
