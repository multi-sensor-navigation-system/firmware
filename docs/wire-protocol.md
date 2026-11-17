# Wire protocol v1

How the firmware and the host talk over UART0 (921600 baud, 8N1). Most
traffic is device to host; the host can send a small set of commands using
the same framing.

## Why not CSV

One CSV row is about 177 bytes. At 500 Hz that is 88.5 kB/s on a link that
carries at most 92.16 kB/s, so the UART ran about 96 % full before any
status lines, and any stall dropped rows. A binary sample frame is 103 bytes
at most (51.5 kB/s, about 56 % of the link), carries a checksum, and
resynchronises after corruption.

The CSV stream still exists as a debug build: `pio run -e esp32dev-csv`.

## Framing

```
frame = COBS( type:u8 | body | crc16:u16le )  0x00
```

- **COBS** (Consistent Overhead Byte Stuffing) removes every `0x00` from the
  payload, so `0x00` only ever appears as the frame delimiter. A reader that
  attaches mid-stream discards bytes up to the next `0x00` and is in sync.
- **crc16** is CRC-16/CCITT-FALSE (poly `0x1021`, init `0xFFFF`) over
  `type | body`. Check value: `"123456789"` → `0x29B1`.
- All multi-byte fields are little-endian.
- A frame whose CRC fails, or whose type is unknown, is dropped; the stream
  continues at the next delimiter.
- Empty frames (two delimiters in a row) are ignored. The firmware writes one
  `0x00` at boot so the boot ROM's output cannot corrupt the first frame.

## Frame types

| Type | Name | Body |
|---|---|---|
| `0x01` | `HELLO`  | `proto:u8` then ASCII `key=value;key=value` |
| `0x02` | `SAMPLE` | 98-byte packed struct, below |
| `0x03` | `STATUS` | `rb_drops:u32`, `samples_sent:u32` |
| `0x04` | `LOG`    | ASCII text (sensor init results, warnings) |
| `0x05` | `ACK`    | `cmd:u8`, `status:u8`, `signal:u8` — the answer to a command |

Host to device:

| Type | Name | Body |
|---|---|---|
| `0x10` | `SET_PINS`   | `sda:u8`, `scl:u8`, `gps_rx:u8`, `gps_tx:u8` (GPIO numbers) |
| `0x11` | `RESET_PINS` | empty |

### HELLO

Sent once at boot, before the first sample, and repeated every 2 s so a host
can attach without resetting the board. `proto` is the protocol version (1).
Keys:

| Key | Example | Meaning |
|---|---|---|
| `board` | `esp32dev` | Board the firmware was built for |
| `fw` | `0.2.0` | Firmware version |
| `git` | `abc1234` | Git revision of the build |
| `imu_hz`, `mag_hz`, `baro_hz`, `gps_hz` | `500` | Configured sample rates |
| `sensors` | `imu,mag,baro,gps` | Sensors that initialised |
| `id` | `7cdfa1e2b3c4` | Unique id of this chip: its factory MAC address, 12 hex digits |
| `pins` | `21,22,16,17` | GPIOs in use, in the order `sda,scl,gps_rx,gps_tx` |
| `pin_src` | `default` or `stored` | Whether the pins are the board defaults or were set on this device |

Hosts must ignore keys they do not know, and must refuse a `proto` they do
not support.

### SAMPLE

| Offset | Field | Type | Unit |
|---|---|---|---|
| 0 | `seq` | u32 | sample counter; a gap means frames were lost on the wire |
| 4 | `t_imu_us` | u64 | µs, `esp_timer_get_time()` at IMU read start |
| 12 | `proc_us` | u32 | µs spent reading all sensors for this row |
| 16 | `t_gps_us` | u64 | µs, timestamp of last parsed GPS sentence |
| 24 | `ax, ay, az` | 3 × f32 | g |
| 36 | `gx, gy, gz` | 3 × f32 | °/s |
| 48 | `mx, my, mz` | 3 × f32 | µT |
| 60 | `pressure` | f32 | hPa |
| 64 | `bmp_temp` | f32 | °C |
| 68 | `lat, lon` | 2 × f64 | degrees |
| 84 | `alt_gps` | f32 | m (MSL) |
| 88 | `hdop` | f32 | — |
| 92 | `satellites` | u8 | — |
| 93 | `fault_flag` | u8 | bitmask, see README |
| 94 | `jitter_us` | u32 | µs past the IMU deadline |

The CSV columns map one-to-one, with `t_read_start_us = t_imu_us` and
`t_proc_end_us = t_imu_us + proc_us`.

### STATUS

Sent once per second. `rb_drops` counts rows the sensor task dropped because
the ring buffer was full (the UART could not keep up). `samples_sent` is the
total number of `SAMPLE` frames written.

## Commands

Each physical board can keep its own pin assignment. The pins are stored in
the chip's non-volatile storage, survive a firmware update, and are lost
only by a full flash erase.

- `SET_PINS` asks the device to use the given GPIOs. The device checks them
  against the pin rules built into its firmware (`chips/<chip>.json` plus
  the board's reserved pins). It never stores an assignment that fails.
- `RESET_PINS` forgets the stored pins; the board defaults apply again.
- The device answers every command with one `ACK`. On `status = 0` it then
  restarts, and the next `HELLO` shows the pins now in use.
- Frames longer than 16 body bytes, and frames whose CRC fails, are dropped
  without an answer. A host should retry if no `ACK` arrives within 1 s.
- The CSV debug build takes no commands.

`ACK.status`:

| Value | Meaning |
|---|---|
| 0 | Done. The device restarts to apply it |
| 1 | The command body has the wrong length |
| 2 | A GPIO does not exist on this chip, or is blocked or reserved |
| 3 | A signal that must be driven was put on an input-only GPIO |
| 4 | Two signals were put on the same GPIO |
| 5 | The setting could not be written |
| 6 | Unknown command |

`ACK.signal` names the first signal the status refers to (`0` sda, `1` scl,
`2` gps_rx, `3` gps_tx), or `0xFF` if it refers to none.

The UART0 pins are always blocked, so no pin assignment can cut the host
link: a board with wrong pins still answers `RESET_PINS`.

## Versioning

`proto` changes whenever an existing frame's layout changes. Adding a new
frame type or a new HELLO key does not change it.

## Reference implementation and tests

- Encoder and command receiver: `include/wire.h`; pin check:
  `include/pin_config.h` (both header-only, no Arduino dependencies).
- Host tests: `tools/host/test_wire.cpp` and `tools/host/test_pins.cpp`,
  run in CI.
- `tools/host/wire_dump.cpp` emits the golden stream that the Python decoder
  in `data-pipeline` is tested against.
