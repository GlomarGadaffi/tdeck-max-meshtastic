#include "variant.h"
#include "ExtensionIOXL9555.hpp"
#include "configuration.h"
#include <Preferences.h>

extern ExtensionIOXL9555 io;

// NVS home for the antenna choice. Keys are limited to 15 characters.
static const char *kAntennaPrefsNamespace = "tdeckmax";
static const char *kAntennaPrefsKey = "extAnt";

// This build ships with the external SMA antenna selected; the internal antenna is opt-in.
static const bool kAntennaDefaultExternal = true;

static void setExpandPin(uint8_t pin, uint8_t value)
{
    io.pinMode(pin, OUTPUT);
    io.digitalWrite(pin, value);
}

bool tdeckMaxUseExternalAntenna()
{
    Preferences prefs;
    // begin() fails when the namespace has never been written, so fall back to the default.
    if (!prefs.begin(kAntennaPrefsNamespace, true))
        return kAntennaDefaultExternal;
    bool external = prefs.getBool(kAntennaPrefsKey, kAntennaDefaultExternal);
    prefs.end();
    return external;
}

void tdeckMaxSetAntenna(bool external)
{
    // EXPANDS_LORA_SEL: HIGH selects the internal antenna, LOW the external SMA connector.
    setExpandPin(EXPANDS_LORA_SEL, external ? LOW : HIGH);

    Preferences prefs;
    if (prefs.begin(kAntennaPrefsNamespace, false)) {
        prefs.putBool(kAntennaPrefsKey, external);
        prefs.end();
    }

    LOG_INFO("LoRa antenna: %s", external ? "external" : "internal");
}

static void pulseExpandPinLow(uint8_t pin, uint32_t lowMs, uint32_t highMs)
{
    setExpandPin(pin, LOW);
    delay(lowMs);
    io.digitalWrite(pin, HIGH);
    delay(highMs);
}

void earlyInitVariant()
{
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
    pinMode(PIN_EINK_CS, OUTPUT);
    digitalWrite(PIN_EINK_CS, HIGH);
    pinMode(KB_INT, INPUT_PULLUP);
    pinMode(CST328_PIN_INT, INPUT_PULLUP);
    pinMode(PIN_EINK_BL, OUTPUT);
    analogWrite(PIN_EINK_BL, 0);

    io.begin(Wire, XL9555_SLAVE_ADDRESS0, SDA, SCL);
    setExpandPin(EXPANDS_MODEM_EN, LOW);
    setExpandPin(EXPANDS_MODEM_PWRKEY, LOW);
    setExpandPin(EXPANDS_LORA_EN, HIGH);
    // Apply the saved antenna choice directly rather than via tdeckMaxSetAntenna(), so a plain
    // boot does not rewrite NVS.
    bool useExternalAntenna = tdeckMaxUseExternalAntenna();
    // No LOG_INFO here: earlyInitVariant() runs before the logging subsystem is up, and
    // RedirectablePrint::log() panics if called this early.
    setExpandPin(EXPANDS_LORA_SEL, useExternalAntenna ? LOW : HIGH);
    setExpandPin(EXPANDS_GPS_EN, HIGH);
    setExpandPin(EXPANDS_1V8_EN, HIGH);
    setExpandPin(EXPANDS_DRV_EN, HIGH);
    setExpandPin(EXPANDS_AMP_EN, LOW);
    setExpandPin(EXPANDS_AUDIO_SEL, LOW);
    pulseExpandPinLow(EXPANDS_TOUCH_RST, 20, 60);
    pulseExpandPinLow(EXPANDS_KB_RST, 20, 60);
}
