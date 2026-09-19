/*
 * port_function.cpp  –  ESP32-RC-Sound v6.x (Port-B-Rollen-Registry)
 *
 * Bildet jede PortBMode-Rolle (siehe port_function.h) auf ihr Begin/Update/
 * IsActive-Modul ab. setup()/loop() im .ino kennen keine rollenspezifischen
 * Details mehr - sie rufen nur noch getPortBModule(config.PortB_Mode)->
 * begin()/->update() auf.
 */

#include "port_function.h"
#include "sport_lipo.h"
#include "crsf_esp32.h"
#include <cstring>

// Port B kann als zweite, unabhaengige CRSF-Instanz laufen (siehe
// crsf_esp32.h/.cpp: serial_data/linkStats/info sind Member statt globaler
// Variablen, genau damit eine zweite Instanz Port A nicht mitbenutzt). Als
// globales Objekt im .ino deklariert (CRSF crsfPortB;), hier per extern
// genutzt, damit die begin/update-Wrapper unten sie ansprechen koennen.
extern CRSF crsfPortB;

// isActive()-Zustand fuer die Rollen, die keinen eigenen, dafuer geeigneten
// Zustand nach aussen anbieten (S.Port hat mit sportLipoIsActive() bereits
// einen echten Uebernahme-Getter, siehe sport_lipo.h/.cpp).
static bool portBCrsfActive = false;
static bool portBRawActive  = false;

static void portBCrsfBegin(HardwareSerial* serial, uint8_t rxPin, uint8_t txPin, uint32_t baud) {
    crsfPortB.init_crsf(serial, rxPin, txPin, baud);
    portBCrsfActive = true;
    Serial.printf("Port B: CRSF-Zusatzgeraet (RX=%d, TX=%d, %u Baud)\n", rxPin, txPin, (unsigned)baud);
}
static void portBCrsfUpdate() {
    // Frames werden geparst/gezaehlt (Debug-Seite zeigt gueltige Frames/CRC-
    // Fehler ueber crsfPortB.getValidFrames() etc.) - eine Kanal-/Telemetrie-
    // Auswertung des Zusatzgeraets ist noch nicht angebunden und folgt bei
    // Bedarf in einem spaeteren Schritt.
    crsfPortB.read_packets(0);
}
static bool portBCrsfIsActive() { return portBCrsfActive; }

static void portBRawBegin(HardwareSerial* serial, uint8_t rxPin, uint8_t txPin, uint32_t baud) {
    serial->begin(baud, SERIAL_8N1, rxPin, txPin);
    portBRawActive = true;
    Serial.printf("Port B: Raw-UART (RX=%d, TX=%d, %u Baud) - Firmware wertet nichts aus, Pins liegen fuer ein eigenes Geraet an.\n", rxPin, txPin, (unsigned)baud);
}
static void portBRawUpdate() {}
static bool portBRawIsActive() { return portBRawActive; }

static void portBHottBegin(HardwareSerial* serial, uint8_t rxPin, uint8_t txPin, uint32_t baud) {
    // In Vorbereitung: im Menue/in der Konfiguration waehlbar, aber noch
    // kein Hott-Telemetrie-Treiber vorhanden. Bewusst KEIN UART-Init hier,
    // damit nichts Halbfertiges auf den Pins liegt.
    (void)serial; (void)rxPin; (void)txPin; (void)baud;
    Serial.println("Port B: Hott-Telemetrie gewaehlt, Treiber aber noch nicht implementiert (in Vorbereitung) - Port bleibt inaktiv.");
}
static void portBHottUpdate() {}
static bool portBHottIsActive() { return false; }

// Index == numerischer PortBMode-Wert (siehe config.h). Index 0 (PORTB_OFF)
// wird nie ueber dieses Array aufgeloest (getPortBModule() liefert dafuer
// direkt nullptr), ist hier aber der Vollstaendigkeit halber mit "Aus"
// benannt, falls irgendwo doch per Index in die Tabelle gegriffen wird.
static const PortBFunctionModule PORTB_MODULES[PORTB_FUNCTION_COUNT] = {
    { "Aus",         0,                        nullptr,        nullptr,         nullptr },            // PORTB_OFF
    { "CRSF",        PORTB_BAUD_DEFAULT_CRSF,  portBCrsfBegin, portBCrsfUpdate, portBCrsfIsActive },   // PORTB_CRSF
    { "S.Port",      PORTB_BAUD_DEFAULT_SPORT, sportLipoInit,  sportLipoUpdate, sportLipoIsActive },   // PORTB_SPORT
    { "Hott (bald)", PORTB_BAUD_DEFAULT_HOTT,  portBHottBegin, portBHottUpdate, portBHottIsActive },   // PORTB_HOTT
    { "Raw",         PORTB_BAUD_DEFAULT_RAW,   portBRawBegin,  portBRawUpdate,  portBRawIsActive },    // PORTB_RAW
};

const PortBFunctionModule* getPortBModule(PortBMode f) {
    uint8_t idx = (uint8_t)f;
    if (idx == (uint8_t)PORTB_OFF) return nullptr;
    if (idx >= PORTB_FUNCTION_COUNT) return nullptr;
    return &PORTB_MODULES[idx];
}

const char* portBFunctionName(PortBMode f) {
    uint8_t idx = (uint8_t)f;
    if (idx >= PORTB_FUNCTION_COUNT) idx = (uint8_t)PORTB_OFF;
    return PORTB_MODULES[idx].name;
}

const char* portBFunctionOptionsString() {
    // Statisch aus der Tabelle aufgebaut (einmalig) - Reihenfolge/Anzahl
    // muss zur Enum-Reihenfolge passen (per Kommentar in port_function.h
    // sichergestellt). Bei neuen Rollen hier + in PORTB_MODULES +
    // PORTB_FUNCTION_COUNT (config.h) ergaenzen.
    static char buf[64] = {0};
    if (buf[0] == '\0') {
        for (uint8_t i = 0; i < PORTB_FUNCTION_COUNT; i++) {
            if (i > 0) strncat(buf, ";", sizeof(buf) - strlen(buf) - 1);
            strncat(buf, PORTB_MODULES[i].name, sizeof(buf) - strlen(buf) - 1);
        }
    }
    return buf;
}

uint32_t portBFunctionDefaultBaud(PortBMode f) {
    uint8_t idx = (uint8_t)f;
    if (idx >= PORTB_FUNCTION_COUNT) return 0;
    return PORTB_MODULES[idx].defaultBaud;
}

PortBMode clampPortBMode(int raw) {
    if (raw < 0 || raw >= (int)PORTB_FUNCTION_COUNT) return PORTB_OFF;
    return (PortBMode)raw;
}
