#pragma once
#include "pin_config.h"

// ── PER-DEVICE SETTINGS ────────────────────────────────
// Settings that belong to one physical board, kept in the chip's
// non-volatile storage so they survive a firmware update.

// Unique id of this chip: the factory MAC address as 12 lowercase hex
// digits, in the order esptool prints it.
const char *device_id();

// Pins in use: the stored ones if present and still allowed by this
// build's rules, otherwise the board manifest defaults. Call once at boot.
PinConfig device_pins_load();

// True if device_pins_load() returned stored pins rather than defaults.
bool device_pins_stored();

// Check and store. Returns PIN_OK only if the pins were written; they take
// effect at the next boot. `signal` receives the offending signal, if any.
PinStatus device_pins_save(const PinConfig &pins, uint8_t *signal);

// Forget the stored pins. Returns PIN_OK or PIN_ERR_STORAGE.
PinStatus device_pins_clear();
