#include <Arduino.h>
#include <Preferences.h>

#include "config.h"
#include "device_config.h"

static const char NVS_NAMESPACE[] = "msns";
static const char NVS_KEY_PINS[]  = "pins";

static const PinConfig DEFAULT_PINS = {PIN_SDA, PIN_SCL, PIN_GPS_RX, PIN_GPS_TX};
static bool s_stored = false;

const char *device_id() {
    static char id[13] = "";
    if (!id[0]) {
        // getEfuseMac() packs the first MAC octet into the lowest byte.
        uint64_t mac = ESP.getEfuseMac();
        for (int i = 0; i < 6; i++)
            snprintf(id + 2 * i, 3, "%02x", (unsigned)((mac >> (8 * i)) & 0xFF));
    }
    return id;
}

PinConfig device_pins_load() {
    PinConfig pins = DEFAULT_PINS;
    s_stored = false;

    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, true)) {
        uint8_t raw[PIN_SIG_COUNT];
        if (prefs.getBytesLength(NVS_KEY_PINS) == sizeof(raw) &&
            prefs.getBytes(NVS_KEY_PINS, raw, sizeof(raw)) == sizeof(raw)) {
            PinConfig stored = {raw[0], raw[1], raw[2], raw[3]};
            // Checked again here: the rules may be stricter in this build
            // than in the one that stored the pins.
            if (pincfg_check(stored, MSNS_PIN_MASK_OUT, MSNS_PIN_MASK_IN) == PIN_OK) {
                pins = stored;
                s_stored = true;
            }
        }
        prefs.end();
    }
    return pins;
}

bool device_pins_stored() { return s_stored; }

PinStatus device_pins_save(const PinConfig &pins, uint8_t *signal) {
    PinStatus status = pincfg_check(pins, MSNS_PIN_MASK_OUT, MSNS_PIN_MASK_IN, signal);
    if (status != PIN_OK) return status;

    const uint8_t raw[PIN_SIG_COUNT] = {pins.sda, pins.scl, pins.gps_rx, pins.gps_tx};
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return PIN_ERR_STORAGE;
    size_t written = prefs.putBytes(NVS_KEY_PINS, raw, sizeof(raw));
    prefs.end();
    return written == sizeof(raw) ? PIN_OK : PIN_ERR_STORAGE;
}

PinStatus device_pins_clear() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return PIN_ERR_STORAGE;
    // remove() reports false when the key was never set; that is still "cleared".
    if (prefs.isKey(NVS_KEY_PINS)) prefs.remove(NVS_KEY_PINS);
    bool gone = !prefs.isKey(NVS_KEY_PINS);
    prefs.end();
    return gone ? PIN_OK : PIN_ERR_STORAGE;
}
