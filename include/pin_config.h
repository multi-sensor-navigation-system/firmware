#pragma once
// ── PIN CONFIGURATION ──────────────────────────────────
// Which GPIO carries each sensor signal on this particular board, and the
// check that decides whether an assignment is allowed. Header-only and free
// of Arduino dependencies so the host tests compile the same code.
//
// The allowed-pin masks come from chips/<chip>.json and the board manifest
// (scripts/boards.py: pin_masks). Bit n set = GPIO n may be used.

#include <stdint.h>

struct PinConfig {
  uint8_t sda;
  uint8_t scl;
  uint8_t gps_rx;
  uint8_t gps_tx;
};

// Order of the signals in a SET_PINS command and in the HELLO `pins` key.
enum PinSignal : uint8_t {
  PIN_SIG_SDA = 0,
  PIN_SIG_SCL = 1,
  PIN_SIG_GPS_RX = 2,
  PIN_SIG_GPS_TX = 3,
  PIN_SIG_COUNT = 4,
  PIN_SIG_NONE = 0xFF,
};

// Also the `status` byte of an ACK frame (docs/wire-protocol.md).
enum PinStatus : uint8_t {
  PIN_OK = 0,
  PIN_ERR_LENGTH = 1,      // command body has the wrong size
  PIN_ERR_NOT_ALLOWED = 2, // GPIO does not exist, or is blocked or reserved
  PIN_ERR_INPUT_ONLY = 3,  // signal needs an output, GPIO can only read
  PIN_ERR_SHARED = 4,      // two signals on one GPIO
  PIN_ERR_STORAGE = 5,     // could not write the setting
  PIN_ERR_UNKNOWN = 6,     // unknown command
};

// Check an assignment. `out_mask`: GPIOs that can drive a signal.
// `in_mask`: GPIOs that can read one (a superset of out_mask). On failure
// `*signal` is the first offending signal.
inline PinStatus pincfg_check(const PinConfig &c, uint64_t out_mask, uint64_t in_mask,
                              uint8_t *signal = nullptr) {
  const uint8_t pin[PIN_SIG_COUNT] = {c.sda, c.scl, c.gps_rx, c.gps_tx};
  PinStatus status = PIN_OK;
  uint8_t at = PIN_SIG_NONE;
  for (uint8_t i = 0; i < PIN_SIG_COUNT && status == PIN_OK; i++) {
    bool can_read = pin[i] < 64 && ((in_mask >> pin[i]) & 1);
    bool can_drive = pin[i] < 64 && ((out_mask >> pin[i]) & 1);
    if (!can_read) {
      status = PIN_ERR_NOT_ALLOWED;
    } else if (i != PIN_SIG_GPS_RX && !can_drive) {
      status = PIN_ERR_INPUT_ONLY; // only gps_rx may sit on an input-only GPIO
    } else {
      for (uint8_t k = 0; k < i; k++)
        if (pin[k] == pin[i])
          status = PIN_ERR_SHARED;
    }
    if (status != PIN_OK)
      at = i;
  }
  if (signal)
    *signal = at;
  return status;
}
