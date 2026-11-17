#pragma once
// ── WIRE PROTOCOL v1 ───────────────────────────────────
// Framing used on the host UART. Header-only and free of Arduino
// dependencies so the same code is compiled into the host-side tests
// (tools/host/) and into the firmware.
//
//   frame   = COBS( type:u8 | body | crc16:u16le ) 0x00
//   crc16   = CRC-16/CCITT-FALSE over type|body
//
// COBS removes every 0x00 from the payload, so 0x00 is an unambiguous
// frame delimiter: a reader that joins mid-stream resynchronises at the
// next delimiter, and a corrupted frame costs exactly one frame.
// All multi-byte fields are little-endian (native on ESP32 and on x86/ARM
// hosts). Full specification: docs/wire-protocol.md

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define WIRE_PROTO_VERSION 1

enum WireType : uint8_t {
  WIRE_HELLO  = 0x01, // proto:u8 | "key=value;key=value" (ASCII, no NUL)
  WIRE_SAMPLE = 0x02, // WireSample
  WIRE_STATUS = 0x03, // WireStatus
  WIRE_LOG    = 0x04, // ASCII text, no NUL
  WIRE_ACK    = 0x05, // WireAck: the answer to a command

  // Host → device commands. Same framing as everything else.
  WIRE_CMD_SET_PINS   = 0x10, // sda:u8 | scl:u8 | gps_rx:u8 | gps_tx:u8
  WIRE_CMD_RESET_PINS = 0x11, // empty: forget the stored pins
};

#pragma pack(push, 1)
struct WireSample {
  uint32_t seq;      // increments per sample sent; gaps = frames lost on the wire
  uint64_t t_imu_us; // esp_timer_get_time() at IMU read start
  uint32_t proc_us;  // t_proc_end - t_imu (saturating)
  uint64_t t_gps_us; // timestamp of last GPS sentence parsed
  float ax, ay, az;  // g
  float gx, gy, gz;  // °/s
  float mx, my, mz;  // µT
  float pressure;    // hPa
  float bmp_temp;    // °C
  double lat, lon;   // degrees
  float alt_gps;     // m (MSL)
  float hdop;
  uint8_t satellites;
  uint8_t fault_flag;
  uint32_t jitter_us;
};

struct WireStatus {
  uint32_t rb_drops;     // rows dropped because the ring buffer was full
  uint32_t samples_sent; // total WIRE_SAMPLE frames written
};

struct WireAck {
  uint8_t cmd;    // the command type being answered
  uint8_t status; // PinStatus: 0 = done, the device restarts to apply it
  uint8_t signal; // PinSignal the status refers to, 0xFF if none
};
#pragma pack(pop)

static_assert(sizeof(WireSample) == 98, "WireSample layout is part of the wire protocol");
static_assert(sizeof(WireStatus) == 8, "WireStatus layout is part of the wire protocol");
static_assert(sizeof(WireAck) == 3, "WireAck layout is part of the wire protocol");

#define WIRE_MAX_BODY 200
// type + body + crc, plus COBS overhead (1 byte per 254) and the delimiter.
#define WIRE_MAX_FRAME (WIRE_MAX_BODY + 3 + (WIRE_MAX_BODY + 3) / 254 + 1 + 1)

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final XOR.
inline uint16_t wire_crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

// Consistent Overhead Byte Stuffing. Writes the encoded bytes (never 0x00)
// to `out` and returns their count. `out` needs len + len/254 + 1 bytes.
inline size_t wire_cobs_encode(const uint8_t *in, size_t len, uint8_t *out) {
  size_t code_idx = 0, w = 1;
  uint8_t code = 1;
  for (size_t i = 0; i < len; i++) {
    if (in[i] == 0) {
      out[code_idx] = code;
      code_idx = w++;
      code = 1;
    } else {
      out[w++] = in[i];
      if (++code == 0xFF) {
        out[code_idx] = code;
        code_idx = w++;
        code = 1;
      }
    }
  }
  out[code_idx] = code;
  return w;
}

// Build one complete frame, delimiter included. Returns the frame length,
// or 0 if the body is too large. `out` must hold WIRE_MAX_FRAME bytes.
inline size_t wire_frame(uint8_t type, const uint8_t *body, size_t len, uint8_t *out) {
  if (len > WIRE_MAX_BODY)
    return 0;
  uint8_t raw[WIRE_MAX_BODY + 3];
  raw[0] = type;
  if (len)
    memcpy(raw + 1, body, len);
  uint16_t crc = wire_crc16(raw, len + 1);
  raw[len + 1] = (uint8_t)(crc & 0xFF);
  raw[len + 2] = (uint8_t)(crc >> 8);
  size_t n = wire_cobs_encode(raw, len + 3, out);
  out[n++] = 0x00;
  return n;
}

// ── RECEIVING (host → device commands) ─────────────────

// Inverse of wire_cobs_encode. Returns the decoded length, or -1 if the
// input is malformed or does not fit in `cap`.
inline int wire_cobs_decode(const uint8_t *in, size_t len, uint8_t *out, size_t cap) {
  size_t r = 0, w = 0;
  while (r < len) {
    uint8_t code = in[r++];
    if (code == 0)
      return -1;
    for (uint8_t k = 1; k < code; k++) {
      if (r >= len || w >= cap || in[r] == 0)
        return -1;
      out[w++] = in[r++];
    }
    if (code != 0xFF && r < len) {
      if (w >= cap)
        return -1;
      out[w++] = 0;
    }
  }
  return (int)w;
}

// Commands are tiny; anything longer than this is not for us.
#define WIRE_RX_MAX_BODY 16
#define WIRE_RX_MAX_FRAME (WIRE_RX_MAX_BODY + 3 + 1)

// Collects bytes up to each 0x00 delimiter and hands back one checked
// command at a time. Oversized or corrupt frames are dropped whole.
struct WireReceiver {
  uint8_t buf[WIRE_RX_MAX_FRAME];
  uint8_t len = 0;
  bool overflow = false;
  uint8_t body[WIRE_RX_MAX_BODY];
  uint8_t body_len = 0;
  uint8_t type = 0;

  // Feed one received byte. Returns true when `type`, `body` and
  // `body_len` hold a complete frame whose CRC is correct.
  bool feed(uint8_t byte) {
    if (byte != 0x00) {
      if (len < sizeof(buf))
        buf[len++] = byte;
      else
        overflow = true;
      return false;
    }
    bool ok = false;
    uint8_t raw[WIRE_RX_MAX_BODY + 3];
    if (!overflow && len > 0) {
      int n = wire_cobs_decode(buf, len, raw, sizeof(raw));
      if (n >= 3) {
        uint16_t crc = (uint16_t)(raw[n - 2] | (raw[n - 1] << 8));
        if (crc == wire_crc16(raw, (size_t)n - 2)) {
          type = raw[0];
          body_len = (uint8_t)(n - 3);
          memcpy(body, raw + 1, body_len);
          ok = true;
        }
      }
    }
    len = 0;
    overflow = false;
    return ok;
  }
};
