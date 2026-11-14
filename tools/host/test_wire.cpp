// Host-side tests for the wire protocol (include/wire.h).
//   g++ -std=c++17 -Wall -Wextra -Werror -Iinclude tools/host/test_wire.cpp -o test_wire && ./test_wire
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

#include "wire.h"

// Reference COBS decoder, written independently of the encoder under test.
static std::vector<uint8_t> cobs_decode(const uint8_t *in, size_t len) {
  std::vector<uint8_t> out;
  size_t i = 0;
  while (i < len) {
    uint8_t code = in[i++];
    assert(code != 0);
    for (uint8_t k = 1; k < code; k++) {
      assert(i < len);
      out.push_back(in[i++]);
    }
    if (code != 0xFF && i < len)
      out.push_back(0);
  }
  return out;
}

static void check_roundtrip(const std::vector<uint8_t> &body) {
  uint8_t frame[WIRE_MAX_FRAME];
  size_t n = wire_frame(WIRE_SAMPLE, body.data(), body.size(), frame);
  assert(n > 0 && n <= WIRE_MAX_FRAME);
  assert(frame[n - 1] == 0x00);
  for (size_t i = 0; i + 1 < n; i++)
    assert(frame[i] != 0x00); // the delimiter is the only zero byte

  std::vector<uint8_t> raw = cobs_decode(frame, n - 1);
  assert(raw.size() == body.size() + 3);
  assert(raw[0] == WIRE_SAMPLE);
  assert(memcmp(raw.data() + 1, body.data(), body.size()) == 0);
  uint16_t crc = (uint16_t)(raw[raw.size() - 2] | (raw[raw.size() - 1] << 8));
  assert(crc == wire_crc16(raw.data(), raw.size() - 2));
}

int main() {
  // Standard check value for CRC-16/CCITT-FALSE.
  assert(wire_crc16((const uint8_t *)"123456789", 9) == 0x29B1);

  check_roundtrip({});
  check_roundtrip({0});
  check_roundtrip({0, 0, 0});
  check_roundtrip({1, 2, 3});
  check_roundtrip({1, 0, 2, 0, 0, 3});
  check_roundtrip(std::vector<uint8_t>(WIRE_MAX_BODY, 0x00));
  check_roundtrip(std::vector<uint8_t>(WIRE_MAX_BODY, 0xAB)); // no zeros: forces 0xFF code blocks
  for (size_t len = 0; len <= WIRE_MAX_BODY; len++) {
    std::vector<uint8_t> body(len);
    for (size_t i = 0; i < len; i++)
      body[i] = (uint8_t)((i * 37 + len) % 5 == 0 ? 0 : i * 31 + len);
    check_roundtrip(body);
  }

  // Oversized bodies are rejected rather than truncated.
  std::vector<uint8_t> big(WIRE_MAX_BODY + 1, 1);
  uint8_t frame[WIRE_MAX_FRAME];
  assert(wire_frame(WIRE_LOG, big.data(), big.size(), frame) == 0);

  // A sample frame is 103 bytes at most: 98 body + type + crc + COBS + delimiter.
  WireSample s;
  memset(&s, 0x11, sizeof(s));
  assert(wire_frame(WIRE_SAMPLE, (const uint8_t *)&s, sizeof(s), frame) == 103);

  puts("wire protocol: all host tests passed");
  return 0;
}
