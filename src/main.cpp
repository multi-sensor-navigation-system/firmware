#include <Arduino.h>
#include <Wire.h>
#include <HardwareSerial.h>

#include <Adafruit_MPU6050.h>
#include <QMC5883LCompass.h>
#include <Adafruit_BMP280.h>
#include <TinyGPSPlus.h>

#include "config.h"
#include "esp_task_wdt.h"
#include "sensor_row.h"

// ── GLOBALS ────────────────────────────────────────────
Adafruit_MPU6050    mpu;
QMC5883LCompass   mag;
Adafruit_BMP280     bmp;
TinyGPSPlus         gps;
HardwareSerial      gpsSerial(GPS_UART_NUM);

RingBuffer          ring = {};         // zero-init

// Last known good GPS state — written and read exclusively by task_sensors
// (Core 1). task_serial (Core 0) never touches these globals directly;
// it only reads rows already in the ring buffer.
// Cross-core access exists but is benign for logging (no RMW, snapshot semantics).
volatile double  gps_lat       = 0.0;
volatile double  gps_lon       = 0.0;
volatile float   gps_alt       = 0.0f;
volatile float   gps_hdop      = 99.0f;
volatile uint8_t gps_sats      = 0;
volatile bool    gps_valid     = false;
volatile uint64_t gps_ts_us    = 0;
volatile uint32_t rb_drops = 0;

// ── CSV HEADER ─────────────────────────────────────────
static const char CSV_HEADER[] =
    "t_imu_us,t_read_start_us,t_proc_end_us,t_gps_us,"
    "ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,"
    "mx_uT,my_uT,mz_uT,"
    "pressure_hPa,bmp_temp_C,"
    "lat_deg,lon_deg,alt_gps_m,hdop,satellites,"
    "fault_flag,jitter_us";

// ── FORWARD DECLS ──────────────────────────────────────
void task_sensors(void* pv);
void task_serial(void* pv);
void print_row(const SensorRow& r);
bool init_sensors();

// ══════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════
void setup() {
    Serial.begin(HOST_BAUD);
    while (!Serial) delay(10);

    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(I2C_FREQ_HZ);

    gpsSerial.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);

    if (!init_sensors()) {
        Serial.println("# WARN: some sensors missing — logging available sensors only.");
    }

    // Print header once — Python logger watches for this line
    Serial.println(CSV_HEADER);

    // Deregister idle tasks from the task watchdog.
    // Our sensor task on Core 1 limits idle task runtime — without this
    // the TWDT fires every ~5 s even with sdkconfig.defaults disabled.
    esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(0));
    esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(1));

    // Sensor task → Core 1, high priority
    xTaskCreatePinnedToCore(task_sensors, "sensors", 8192, nullptr, 5, nullptr, 1);

    // Serial task → Core 0 (same core as Arduino loop/WiFi), lower priority
    xTaskCreatePinnedToCore(task_serial,  "serial",  4096, nullptr, 3, nullptr, 0);
}

void loop() {
    // Deregister loopTask from the TWDT then suspend permanently.
    // vTaskDelay(portMAX_DELAY) looks like a hang to the watchdog
    // and causes a hard reset every ~5 seconds without this.
    esp_task_wdt_delete(NULL);
    vTaskSuspend(NULL);
}

// ══════════════════════════════════════════════════════
//  SENSOR INIT
// ══════════════════════════════════════════════════════
bool init_sensors() {
    bool ok = true;

    // MPU-6050
    if (!mpu.begin()) {
        Serial.println("# MPU-6050 not found");
        ok = false;
    } else {
        mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
        mpu.setGyroRange(MPU6050_RANGE_500_DEG);
        mpu.setFilterBandwidth(MPU6050_BAND_44_HZ);
        Serial.println("# MPU-6050 OK");
    }

    // QMC5883L — begin() always succeeds (no ACK check in library),
    // Manual I²C probe at 0x0D to confirm it's actually there.
    Wire.beginTransmission(0x0D);
    bool qmc_found = (Wire.endTransmission() == 0);
    if (!qmc_found) {
        Serial.println("# QMC5883L not found (tried 0x0D)");
        ok = false;
    } else {
        mag.init();
        mag.setMode(0x01, 0x0C, 0x10, 0x00); // continuous, 200Hz, 8G, 512 OSR
        Serial.println("# QMC5883L OK");
    }

    // BMP280
    if (!bmp.begin(0x76)) {
        Serial.println("# BMP280 not found (tried 0x76)");
        ok = false;
    } else {
        // STANDBY_MS_63 → BMP280 internal cycle ≈ 63ms → ~15.8Hz updates.
        // STANDBY_MS_500 gives only 2Hz updates despite firmware polling at 25Hz.
        // SAMPLING_X4 on temperature provides better noise rejection w/o slowing cycle (unlike X2).
        bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                        Adafruit_BMP280::SAMPLING_X4,
                        Adafruit_BMP280::SAMPLING_X16, // (pressure) for better accuracy
                        Adafruit_BMP280::FILTER_X16,
                        Adafruit_BMP280::STANDBY_MS_63);
        Serial.println("# BMP280 OK");
    }

    // to confirm GPS UART is alive
    Serial.println("# GPS UART started — waiting for fix (HDOP reported in data)");

    return ok;
}

// ══════════════════════════════════════════════════════
//  TASK: SENSORS  (Core 1)
// ══════════════════════════════════════════════════════
void task_sensors(void* pv) {
    esp_task_wdt_delete(NULL);   // this task intentionally busy-waits

    uint64_t next_imu_us  = esp_timer_get_time() + IMU_PERIOD_US;
    uint64_t next_baro_us = esp_timer_get_time() + BARO_PERIOD_US;
    uint64_t next_mag_us  = esp_timer_get_time() + MAG_PERIOD_US;
    uint64_t next_gps_us  = esp_timer_get_time() + GPS_PERIOD_US;

    // Rolling state — last known-good values for each sensor.
    // Carried forward into each row so consumers always see a valid
    // reading rather than zeros when a sensor misses its deadline.
    float last_ax = 0.0f, last_ay = 0.0f, last_az = 0.0f;
    float last_gx = 0.0f, last_gy = 0.0f, last_gz = 0.0f;
    float last_pressure  = 0.0f;
    float last_bmp_temp  = 0.0f;
    float last_mx = 0.0f, last_my = 0.0f, last_mz = 0.0f;

    for (;;) {
        uint64_t now = esp_timer_get_time();

        if (now < next_imu_us) {
           // Yield if far from deadline; spin for last period for timing precision
            int64_t remaining = (int64_t)(next_imu_us - now);
            if (remaining > IMU_PERIOD_US) vTaskDelay(1);
            continue;
        }

        // ── IMU READ ────────────────────────────────────
        uint64_t t_read_start = esp_timer_get_time();

        // Jitter = how many μs we overshot the absolute deadline.
        // Measured against next_imu_us (before advancing it) so each tick's
        // error is independent — late tick N does not inflate tick N+1's jitter.
        int64_t signed_jitter = (int64_t)(t_read_start - next_imu_us);
        uint32_t jitter = (signed_jitter > 0) ? (uint32_t)signed_jitter : 0;

        next_imu_us     += IMU_PERIOD_US;

        SensorRow row = {};
        row.t_imu_us        = t_read_start;
        row.t_read_start_us = t_read_start;
        row.jitter_us       = jitter;

        // Poll MPU-6050 directly at 500 Hz. The sensor's internal rate is
        // 1 kHz (BAND_44_HZ), so at most every other poll returns the same
        // sample — acceptable, and far better than the ~150μs I²C round-trip
        // cost of probing INT_STATUS on every tick (which inflates apparent
        // rate to 2600 Hz by consuming the deadline budget).
        // Polling at 500 Hz against a 1 kHz internal rate — every other poll
        // may return the same sample. Jitter can also cause occasional skips.
        sensors_event_t a, g, temp_ev;
        if (mpu.getEvent(&a, &g, &temp_ev)) {
            last_ax = a.acceleration.x / 9.80665f;  // m/s² → g
            last_ay = a.acceleration.y / 9.80665f;
            last_az = a.acceleration.z / 9.80665f;
            last_gx = g.gyro.x * (180.0f / M_PI);   // rad/s → °/s
            last_gy = g.gyro.y * (180.0f / M_PI);
            last_gz = g.gyro.z * (180.0f / M_PI);
        } else {
            row.fault_flag |= FAULT_IMU;             // stale values carried forward below
        }
        row.ax = last_ax;
        row.ay = last_ay;
        row.az = last_az;
        row.gx = last_gx;
        row.gy = last_gy;
        row.gz = last_gz;

        // ── BARO (25 Hz) ────────────────────────────────
        if (esp_timer_get_time() >= next_baro_us) {
            float p = bmp.readPressure();
            float t = bmp.readTemperature();
            if (!isnan(p) && p > 80000.0f) {          // sanity: 800–1100 hPa
                last_pressure = p / 100.0f;            // Pa → hPa
                last_bmp_temp = t;
            } else {
                row.fault_flag |= FAULT_BARO;
            }
            next_baro_us += BARO_PERIOD_US;
        }
	// Reject out-of-range values (sensor glitch / I²C noise), retain last valid
        row.pressure = last_pressure;
        row.bmp_temp = last_bmp_temp;

        // ── MAGNETOMETER / QMC5883L (75 Hz) ────────────
        if (esp_timer_get_time() >= next_mag_us) {
            mag.read();
            int mx_raw = mag.getX();
            int my_raw = mag.getY();
            int mz_raw = mag.getZ();
            // QMC5883L at 8G range: 3000 LSB/Gauss, 1 Gauss = 100 μT
            // → scale = 100.0 / 3000.0 = 0.03333 μT/LSB
            const float LSB_TO_UT = 100.0f / 3000.0f;
            last_mx = mx_raw * LSB_TO_UT;
            last_my = my_raw * LSB_TO_UT;
            last_mz = mz_raw * LSB_TO_UT;
            // QMC has no explicit error return; zeros on I²C fault
            if (mx_raw == 0 && my_raw == 0 && mz_raw == 0) {
                row.fault_flag |= FAULT_MAG;
            }
            next_mag_us += MAG_PERIOD_US;
        }
        row.mx = last_mx;
        row.my = last_my;
        row.mz = last_mz;

        // ── GPS: snapshot globals into row ──────────────────
        // UART draining happens in task_serial (Core 0) to avoid ISR
        // starvation on Core 1 from the busy-wait loop. See task_serial.
        if (esp_timer_get_time() >= next_gps_us) {
            if (!gps_valid) {
                row.fault_flag |= FAULT_GPS_NOFIX;
            }
            next_gps_us += GPS_PERIOD_US;
        }

        // GPS globals updated on Core 0, read on Core 1.
	// double writes are non-atomic (2x32-bit), so tearing is possible.
	// Acceptable for logging: occasional corrupted sample is bounded
	// and corrected by subsequent updates.
        row.lat        = gps_lat;
        row.lon        = gps_lon;
        row.alt_gps    = gps_alt;
        row.hdop       = gps_hdop;
        row.satellites = gps_sats;
        row.t_gps_us   = gps_ts_us;

        // HDOP quality flag (independent of fix validity)
        if (gps_hdop > HDOP_RELIABLE || gps_sats < MIN_SATELLITES) {
            row.fault_flag |= FAULT_GPS_HDOP;
        }

        row.t_proc_end_us = esp_timer_get_time();

        // Push to ring buffer; count drops so task_serial can report them.
        if (!rb_push(&ring, row)) rb_drops++;
    }
}

// ══════════════════════════════════════════════════════
//  TASK: SERIAL OUTPUT + GPS DRAIN  (Core 0)
// ══════════════════════════════════════════════════════
// GPS UART draining lives here intentionally. task_sensors (Core 1)
// busy-waits at 500 Hz; putting the UART drain there starves the Core 1
// UART ISR and causes byte drops in NMEA sentences. Core 0 yields
// regularly so UART interrupts fire cleanly.
void task_serial(void* pv) {
    SensorRow row;
    for (;;) {
        // Drain GPS UART and update last-known-good globals.
        // gps.encode() returns true when a complete sentence is committed;
        // we only update globals on a fresh sentence so they are never
        // reset to boot defaults between sentences (sticky semantics).
        bool new_sentence = false;
        while (gpsSerial.available()) {
            if (gps.encode(gpsSerial.read())) new_sentence = true;
        }
        if (new_sentence && gps.location.isValid()) {
            gps_lat   = gps.location.lat();
            gps_lon   = gps.location.lng();
            gps_alt   = (float)gps.altitude.meters();
            gps_hdop  = (float)gps.hdop.hdop();
            gps_sats  = (uint8_t)gps.satellites.value();
            gps_valid = true;
            gps_ts_us = esp_timer_get_time();
        }

        while (rb_pop(&ring, row)) {
            print_row(row);
        }

        // Report ring buffer drops once per second as a firmware comment.
        // Drops indicate the serial output can't keep up with the sensor rate.
        static uint64_t last_drop_report = 0;
        uint64_t now_us = esp_timer_get_time();
        if (rb_drops > 0 && now_us - last_drop_report >= 1000000ULL) {
            uint32_t drops = rb_drops;   // snapshot: report a consistent value from one point in time
            Serial.printf("# rb_drops=%u\n", drops);
            last_drop_report = now_us;
        }

        vTaskDelay(1);
    }
}

// ── FORMAT ROW AS CSV ──────────────────────────────────
void print_row(const SensorRow& r) {
    // Using Serial.print chains avoids sprintf heap allocation.
    // Format: full precision for doubles (lat/lon), 4dp for floats.

    Serial.print(r.t_imu_us);        Serial.print(',');
    Serial.print(r.t_read_start_us); Serial.print(',');
    Serial.print(r.t_proc_end_us);   Serial.print(',');
    Serial.print(r.t_gps_us);        Serial.print(',');

    Serial.print(r.ax, 5);  Serial.print(',');
    Serial.print(r.ay, 5);  Serial.print(',');
    Serial.print(r.az, 5);  Serial.print(',');
    Serial.print(r.gx, 4);  Serial.print(',');
    Serial.print(r.gy, 4);  Serial.print(',');
    Serial.print(r.gz, 4);  Serial.print(',');

    Serial.print(r.mx, 4);  Serial.print(',');
    Serial.print(r.my, 4);  Serial.print(',');
    Serial.print(r.mz, 4);  Serial.print(',');

    Serial.print(r.pressure, 4); Serial.print(',');
    Serial.print(r.bmp_temp, 3); Serial.print(',');

    Serial.print(r.lat, 8);       Serial.print(',');
    Serial.print(r.lon, 8);       Serial.print(',');
    Serial.print(r.alt_gps, 2);   Serial.print(',');
    Serial.print(r.hdop, 2);      Serial.print(',');
    Serial.print(r.satellites);   Serial.print(',');

    Serial.print(r.fault_flag);   Serial.print(',');
    Serial.println(r.jitter_us);
}
// Fault flags describe this sample only (not latched system faults)