#pragma once
#include "sensor_row.h"
#include <stdint.h>

// ── HOST TRANSPORT ─────────────────────────────────────
// Everything the firmware says to the host goes through these calls.
// The default build speaks the binary wire protocol (wire.h); building
// with -DMSNS_WIRE_CSV=1 keeps the original human-readable CSV stream.
//
// Not thread-safe: call from setup() before the tasks start, and from
// task_serial afterwards. task_sensors never touches the UART.

void transport_begin();

// Free-text message (sensor init results, warnings).
void transport_log(const char *msg);

// Identify the firmware. `sensors` is a comma-separated list of the
// sensors that initialised, e.g. "imu,mag,baro,gps".
void transport_hello(const char *sensors);

void transport_sample(const SensorRow &row);

// Call about once per second with the running ring-buffer drop count.
void transport_heartbeat(uint32_t rb_drops, const char *sensors);
