# Changelog

All notable changes to this repository are recorded here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Changed
- **Binary wire protocol (v1)** replaces CSV as the default host stream:
  COBS-framed, CRC-16 checked, with sequence numbers. A sample is at most
  103 bytes instead of about 177, taking the 921600-baud link from about
  96 % to about 56 % utilisation at 500 Hz. See `docs/wire-protocol.md`.
- Host output moved out of `main.cpp` into `src/transport.cpp`.

### Added
- `HELLO` frame announcing board, firmware version, git revision, protocol
  version, sample rates and detected sensors, at boot and every 2 s.
- `STATUS` frame once per second with ring-buffer drops and samples sent.
- `esp32dev-csv` environment: the original CSV stream as a debug build.
- Host-side protocol tests (`tools/host/`), run in CI.

### Added
- MIT `LICENSE` file (the README badge already pointed at one).
- CI build of the `esp32dev` environment on every push and pull request.
- Release workflow: pushing a `v*` tag publishes a merged, single-file
  firmware image and its SHA-256.

### Fixed
- Comment on the GPS globals said they were written by `task_sensors`;
  they are written by `task_serial` and read by `task_sensors`.
