#include <Arduino.h>

#include "config.h"
#include "transport.h"
#include "version.h"
#include "wire.h"

static uint32_t s_samples_sent = 0;

void transport_begin() {
    // A larger TX buffer lets task_serial queue a burst of frames without
    // blocking on the UART FIFO. Must be set before begin().
    Serial.setTxBufferSize(4096);
    Serial.begin(HOST_BAUD);
    while (!Serial) delay(10);
}

static int format_hello(char *buf, size_t size, const char *sensors) {
    return snprintf(buf, size,
                    "board=%s;fw=%s;git=%s;imu_hz=%d;mag_hz=%d;baro_hz=%d;gps_hz=%d;sensors=%s",
                    MSNS_BOARD_ID, MSNS_FW_VERSION, MSNS_GIT_SHA,
                    IMU_HZ, MAG_HZ, BARO_HZ, GPS_HZ, sensors);
}

#if MSNS_WIRE_CSV
// ══════════════════════════════════════════════════════
//  CSV TRANSPORT (debug build)
// ══════════════════════════════════════════════════════
static const char CSV_HEADER[] =
    "t_imu_us,t_read_start_us,t_proc_end_us,t_gps_us,"
    "ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,"
    "mx_uT,my_uT,mz_uT,"
    "pressure_hPa,bmp_temp_C,"
    "lat_deg,lon_deg,alt_gps_m,hdop,satellites,"
    "fault_flag,jitter_us";

void transport_log(const char *msg) {
    Serial.print("# ");
    Serial.println(msg);
}

void transport_hello(const char *sensors) {
    char text[160];
    format_hello(text, sizeof(text), sensors);
    Serial.print("# hello ");
    Serial.println(text);
    // Header once — the host logger watches for this line.
    Serial.println(CSV_HEADER);
}

void transport_sample(const SensorRow &r) {
    // Serial.print chains avoid sprintf heap allocation.
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
    s_samples_sent++;
}

void transport_heartbeat(uint32_t rb_drops, const char *sensors) {
    (void)sensors;
    // Drops indicate the serial output can't keep up with the sensor rate.
    if (rb_drops > 0) Serial.printf("# rb_drops=%u\n", rb_drops);
}

#else
// ══════════════════════════════════════════════════════
//  BINARY TRANSPORT (default) — see wire.h
// ══════════════════════════════════════════════════════
static uint32_t s_heartbeats = 0;

static void send_frame(uint8_t type, const void *body, size_t len) {
    uint8_t frame[WIRE_MAX_FRAME];
    size_t n = wire_frame(type, (const uint8_t *)body, len, frame);
    if (n) Serial.write(frame, n);
}

void transport_log(const char *msg) {
    size_t len = strlen(msg);
    if (len > WIRE_MAX_BODY) len = WIRE_MAX_BODY;
    send_frame(WIRE_LOG, msg, len);
}

void transport_hello(const char *sensors) {
    char body[WIRE_MAX_BODY];
    body[0] = (char)WIRE_PROTO_VERSION;
    int n = format_hello(body + 1, sizeof(body) - 1, sensors);
    if (n < 0) n = 0;
    if ((size_t)n > sizeof(body) - 2) n = sizeof(body) - 2;
    send_frame(WIRE_HELLO, body, (size_t)n + 1);
}

void transport_sample(const SensorRow &r) {
    WireSample s;
    uint64_t proc = r.t_proc_end_us - r.t_read_start_us;
    s.seq        = s_samples_sent++;
    s.t_imu_us   = r.t_imu_us;
    s.proc_us    = (proc > UINT32_MAX) ? UINT32_MAX : (uint32_t)proc;
    s.t_gps_us   = r.t_gps_us;
    s.ax = r.ax; s.ay = r.ay; s.az = r.az;
    s.gx = r.gx; s.gy = r.gy; s.gz = r.gz;
    s.mx = r.mx; s.my = r.my; s.mz = r.mz;
    s.pressure   = r.pressure;
    s.bmp_temp   = r.bmp_temp;
    s.lat        = r.lat;
    s.lon        = r.lon;
    s.alt_gps    = r.alt_gps;
    s.hdop       = r.hdop;
    s.satellites = r.satellites;
    s.fault_flag = r.fault_flag;
    s.jitter_us  = r.jitter_us;
    send_frame(WIRE_SAMPLE, &s, sizeof(s));
}

void transport_heartbeat(uint32_t rb_drops, const char *sensors) {
    WireStatus st = {rb_drops, s_samples_sent};
    send_frame(WIRE_STATUS, &st, sizeof(st));
    // Repeat the hello every other heartbeat so a host that attaches
    // mid-stream can identify the firmware without resetting the board.
    if (++s_heartbeats % 2 == 0) transport_hello(sensors);
}
#endif
