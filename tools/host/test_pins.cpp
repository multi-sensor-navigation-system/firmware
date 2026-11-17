// Host-side tests for command receiving (wire.h) and the pin check
// (pin_config.h).
//   g++ -std=c++17 -Wall -Wextra -Werror -Iinclude tools/host/test_pins.cpp -o test_pins && ./test_pins
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

#include "config.h"
#include "pin_config.h"
#include "wire.h"

static std::vector<uint8_t> frame_of(uint8_t type, const std::vector<uint8_t> &body) {
  uint8_t frame[WIRE_MAX_FRAME];
  size_t n = wire_frame(type, body.data(), body.size(), frame);
  assert(n > 0);
  return std::vector<uint8_t>(frame, frame + n);
}

// Feed bytes; return how many complete frames came out, keeping the last.
static int feed_all(WireReceiver &rx, const std::vector<uint8_t> &bytes) {
  int frames = 0;
  for (uint8_t b : bytes)
    if (rx.feed(b))
      frames++;
  return frames;
}

static void test_receiver() {
  WireReceiver rx;

  // A command round-trips through the encoder and the receiver.
  assert(feed_all(rx, frame_of(WIRE_CMD_SET_PINS, {21, 22, 16, 17})) == 1);
  assert(rx.type == WIRE_CMD_SET_PINS && rx.body_len == 4);
  assert(rx.body[0] == 21 && rx.body[1] == 22 && rx.body[2] == 16 && rx.body[3] == 17);

  // Zero bytes in the body survive the stuffing; an empty body works.
  assert(feed_all(rx, frame_of(WIRE_CMD_SET_PINS, {0, 0, 4, 0})) == 1);
  assert(rx.body_len == 4 && rx.body[0] == 0 && rx.body[2] == 4 && rx.body[3] == 0);
  assert(feed_all(rx, frame_of(WIRE_CMD_RESET_PINS, {})) == 1);
  assert(rx.type == WIRE_CMD_RESET_PINS && rx.body_len == 0);

  // Every body size the receiver accepts.
  for (size_t len = 0; len <= WIRE_RX_MAX_BODY; len++) {
    std::vector<uint8_t> body(len);
    for (size_t i = 0; i < len; i++)
      body[i] = (uint8_t)(i % 3 == 0 ? 0 : 0xF0 + i);
    assert(feed_all(rx, frame_of(0x7E, body)) == 1);
    assert(rx.type == 0x7E && rx.body_len == len);
    assert(memcmp(rx.body, body.data(), len) == 0);
  }

  // Any single corrupted byte is rejected, and the next frame still parses.
  std::vector<uint8_t> good = frame_of(WIRE_CMD_SET_PINS, {21, 22, 16, 17});
  for (size_t i = 0; i + 1 < good.size(); i++) {
    std::vector<uint8_t> bad = good;
    bad[i] ^= 0x55;
    if (bad[i] == 0)
      continue; // that would split the frame instead of corrupting it
    assert(feed_all(rx, bad) == 0);
    assert(feed_all(rx, good) == 1);
  }

  // Oversized frames are dropped whole and do not disturb what follows.
  assert(feed_all(rx, frame_of(WIRE_LOG, std::vector<uint8_t>(WIRE_RX_MAX_BODY + 1, 7))) == 0);
  assert(feed_all(rx, frame_of(WIRE_LOG, std::vector<uint8_t>(WIRE_MAX_BODY, 7))) == 0);
  assert(feed_all(rx, good) == 1);

  // Noise, empty frames and a mid-frame start are ignored.
  assert(feed_all(rx, {0, 0, 0}) == 0);
  assert(feed_all(rx, {0x41, 0x54, 0x0D, 0x0A, 0}) == 0);
  std::vector<uint8_t> tail(good.begin() + 2, good.end());
  assert(feed_all(rx, tail) == 0);
  assert(feed_all(rx, good) == 1);
}

static void test_pin_check() {
  // The esp32dev masks from config.h (no -D given to this build).
  const uint64_t OUT = MSNS_PIN_MASK_OUT, IN = MSNS_PIN_MASK_IN;
  uint8_t sig = 0;

  assert(pincfg_check({21, 22, 16, 17}, OUT, IN, &sig) == PIN_OK && sig == PIN_SIG_NONE);
  assert(pincfg_check({21, 22, 34, 17}, OUT, IN) == PIN_OK); // gps_rx may be input-only

  assert(pincfg_check({6, 22, 16, 17}, OUT, IN, &sig) == PIN_ERR_NOT_ALLOWED && sig == PIN_SIG_SDA);   // flash
  assert(pincfg_check({21, 12, 16, 17}, OUT, IN, &sig) == PIN_ERR_NOT_ALLOWED && sig == PIN_SIG_SCL);  // strapping
  assert(pincfg_check({21, 22, 3, 17}, OUT, IN, &sig) == PIN_ERR_NOT_ALLOWED && sig == PIN_SIG_GPS_RX); // host link
  assert(pincfg_check({21, 22, 16, 24}, OUT, IN, &sig) == PIN_ERR_NOT_ALLOWED && sig == PIN_SIG_GPS_TX); // no such GPIO
  assert(pincfg_check({21, 22, 16, 64}, OUT, IN) == PIN_ERR_NOT_ALLOWED);  // beyond the mask
  assert(pincfg_check({21, 22, 16, 255}, OUT, IN) == PIN_ERR_NOT_ALLOWED);

  assert(pincfg_check({34, 22, 16, 17}, OUT, IN, &sig) == PIN_ERR_INPUT_ONLY && sig == PIN_SIG_SDA);
  assert(pincfg_check({21, 22, 16, 35}, OUT, IN, &sig) == PIN_ERR_INPUT_ONLY && sig == PIN_SIG_GPS_TX);

  assert(pincfg_check({21, 21, 16, 17}, OUT, IN, &sig) == PIN_ERR_SHARED && sig == PIN_SIG_SCL);
  assert(pincfg_check({21, 22, 16, 21}, OUT, IN, &sig) == PIN_ERR_SHARED && sig == PIN_SIG_GPS_TX);

  // With nothing allowed, nothing passes.
  assert(pincfg_check({21, 22, 16, 17}, 0, 0) == PIN_ERR_NOT_ALLOWED);
}

int main() {
  test_receiver();
  test_pin_check();
  puts("commands and pin check: all host tests passed");
  return 0;
}
