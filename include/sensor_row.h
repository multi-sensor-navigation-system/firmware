#pragma once
#include "config.h"
#include <stdint.h>
#include <string.h>

// ── ONE COMPLETE SENSOR ROW ────────────────────────────
// GPS fields carry the last valid fix; fault_flag bit tells
// the reader whether to trust them.
struct SensorRow {
  // timing
  uint64_t t_imu_us;        // esp_timer_get_time() at IMU read start
  uint64_t t_read_start_us; // earliest read start in this row
  uint64_t t_proc_end_us;   // after all sensor reads complete
  uint64_t t_gps_us;        // timestamp of last GPS sentence parsed

  // IMU (MPU-6050)
  float ax, ay, az; // in g
  float gx, gy, gz; // in °/s

  // Magnetometer (QMC5883L)
  float mx, my, mz; // in μT

  // Barometer (BMP280)
  float pressure; // in hPa
  float bmp_temp; // in °C

  // GPS (NEO-6M)
  double lat, lon; // in degrees
  float alt_gps;   // in metres (MSL)
  float hdop;
  uint8_t satellites;

  // meta
  uint8_t fault_flag; // bitmask, see config.h
  uint32_t jitter_us; // overshoot past absolute IMU deadline (μs); 0 if on time
};

// ── LOCK-FREE RING BUFFER ─────────────────────────────
// Single producer (Core 1 sensor task)
// Single consumer (Core 0 serial task)
// head/tail are uint32_t — 32-bit aligned writes are single-instruction
// atomic on Xtensa LX6, so no mutex is needed for the indices.
// rb_push has a release barrier (after row_store, before head advance) and
// rb_pop has an acquire barrier (after row_load, before tail advance) to
// prevent store/load reordering from exposing partially-written rows.

struct RingBuffer {
  volatile SensorRow buf[RING_SIZE];
  volatile uint32_t head; // producer writes here
  volatile uint32_t tail; // consumer reads here
};

// memcpy helpers for volatile buffers
inline void row_store(volatile SensorRow *dst, const SensorRow &src) {
  memcpy((void *)dst, &src, sizeof(SensorRow));
}
inline void row_load(SensorRow &dst, const volatile SensorRow *src) {
  memcpy(&dst, (const void *)src, sizeof(SensorRow));
}

inline bool rb_push(RingBuffer *rb, const SensorRow &row) {
  uint32_t next = (rb->head + 1) & (RING_SIZE - 1);
  if (next == rb->tail)
    return false; // full, drop
  row_store(&rb->buf[rb->head], row);
  // Memory barrier: ensure row bytes are visible to Core 0
  // before the head index advance is. Without this a compiler
  // or CPU store-reorder could expose a partially-written row.
  __sync_synchronize();
  rb->head = next;
  return true;
}

inline bool rb_pop(RingBuffer *rb, SensorRow &out) {
  if (rb->tail == rb->head)
    return false; // empty
  row_load(out, &rb->buf[rb->tail]);
  // Acquire barrier: ensure the row bytes loaded above are fully visible
  // before we advance tail and potentially allow the producer to overwrite
  // this slot. Pairs with the release barrier in rb_push.
  __sync_synchronize();
  rb->tail = (rb->tail + 1) & (RING_SIZE - 1);
  return true;
}
