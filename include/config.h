#pragma once

// ── BOARD PINS ─────────────────────────────────────────
// Set per board from boards/<id>.json by scripts/board.py. The fallbacks
// below are the esp32dev values, for builds made outside PlatformIO.
#ifndef PIN_SDA
#define PIN_SDA 21
#endif
#ifndef PIN_SCL
#define PIN_SCL 22
#endif
#ifndef PIN_GPS_RX
#define PIN_GPS_RX 16
#endif
#ifndef PIN_GPS_TX
#define PIN_GPS_TX 17
#endif
#ifndef GPS_UART_NUM
#define GPS_UART_NUM 2
#endif

// ── I²C ────────────────────────────────────────────────
#define I2C_FREQ_HZ 400000

// ── GPS UART ───────────────────────────────────────────
#define GPS_BAUD 9600

// ── SERIAL (host logging) ──────────────────────────────
#define HOST_BAUD 921600

// ── SAMPLE RATES ──────────────────────────────────────
#define IMU_HZ 500
#define BARO_HZ 25
#define MAG_HZ 75
#define GPS_HZ 1   

// Derived periods
#define IMU_PERIOD_US (1000000UL / IMU_HZ)   // period = 2000µs
#define BARO_PERIOD_US (1000000UL / BARO_HZ) // period = 40000µs
#define MAG_PERIOD_US (1000000UL / MAG_HZ)   // period = 13333µs
#define GPS_PERIOD_US (1000000UL / GPS_HZ)   // period = 1000000µs

// ── EKF / FILTER ──────────────────────────────────────
#define HDOP_RELIABLE 2.5f // GPS becomes unreliable above this value
#define MIN_SATELLITES 4

// ── FAULT FLAGS (bitmask) ──────────────────────────────
// bit 0 = IMU read fail
// bit 1 = MAG read fail
// bit 2 = BARO read fail
// bit 3 = GPS unreliable (HDOP or sat count)
// bit 4 = GPS no fix
#define FAULT_IMU (1 << 0)
#define FAULT_MAG (1 << 1)
#define FAULT_BARO (1 << 2)
#define FAULT_GPS_HDOP (1 << 3)
#define FAULT_GPS_NOFIX (1 << 4)

// ── RING BUFFER ───────────────────────────────────────
#define RING_SIZE 128 // must be power of 2
