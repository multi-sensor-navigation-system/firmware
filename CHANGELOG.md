# Changelog

All notable changes to this repository are recorded here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
- MIT `LICENSE` file (the README badge already pointed at one).
- CI build of the `esp32dev` environment on every push and pull request.
- Release workflow: pushing a `v*` tag publishes a merged, single-file
  firmware image and its SHA-256.

### Fixed
- Comment on the GPS globals said they were written by `task_sensors`;
  they are written by `task_serial` and read by `task_sensors`.
