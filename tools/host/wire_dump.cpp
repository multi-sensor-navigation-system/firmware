// Emits a deterministic wire-protocol stream on stdout. The output is the
// golden fixture the host decoder (data-pipeline) is tested against, so the
// firmware encoder and the Python decoder are checked against each other.
//
//   g++ -std=c++17 -Iinclude tools/host/wire_dump.cpp -o wire_dump
//   ./wire_dump > golden_v1.bin
#include <cstdio>
#include <cstring>

#include "wire.h"

static void emit(uint8_t type, const void *body, size_t len) {
  uint8_t frame[WIRE_MAX_FRAME];
  size_t n = wire_frame(type, (const uint8_t *)body, len, frame);
  fwrite(frame, 1, n, stdout);
}

static void emit_hello() {
  char body[WIRE_MAX_BODY];
  body[0] = (char)WIRE_PROTO_VERSION;
  int n = snprintf(body + 1, sizeof(body) - 1,
                   "board=esp32dev;fw=0.2.0;git=abc1234;imu_hz=500;mag_hz=75;baro_hz=25;gps_hz=1;"
                   "sensors=imu,mag,baro,gps");
  emit(WIRE_HELLO, body, (size_t)n + 1);
}

int main() {
  const char *log = "MPU-6050 OK";
  emit(WIRE_LOG, log, strlen(log));
  emit_hello();

  for (uint32_t i = 0; i < 8; i++) {
    WireSample s;
    memset(&s, 0, sizeof(s));
    s.seq = i;
    s.t_imu_us = 5000000ULL + 2000ULL * i;
    s.proc_us = 600 + i;
    s.t_gps_us = 4000000ULL;
    s.ax = 0.0f;                 // zero bytes exercise the COBS stuffing
    s.ay = -0.02f;
    s.az = 1.0f + 0.001f * i;
    s.gx = 0.5f; s.gy = -0.25f; s.gz = 0.125f * i;
    s.mx = 20.0f; s.my = -5.0f; s.mz = 38.0f;
    s.pressure = 1013.25f;
    s.bmp_temp = 27.5f;
    s.lat = 18.5204303;
    s.lon = 73.8567437;
    s.alt_gps = 560.0f;
    s.hdop = 1.2f;
    s.satellites = 8;
    s.fault_flag = (i == 3) ? 0x06 : 0x00;
    s.jitter_us = 10 * i;
    emit(WIRE_SAMPLE, &s, sizeof(s));
  }

  WireStatus st = {3, 8};
  emit(WIRE_STATUS, &st, sizeof(st));
  return 0;
}
