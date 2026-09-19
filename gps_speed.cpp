/*
 * gps_speed.cpp  –  ESP32-RC-Sound  (Port C)
 *
 * Siehe gps_speed.h. Liest ein NMEA-GPS über Software-Serial (RX=GPIO27,
 * TX=GPIO14) und wertet es mit TinyGPS++ aus. Die Ground Speed kommt direkt
 * aus dem GPS und muss auf dem Modul nicht berechnet werden.
 *
 * Nach dem Boot wird das GPS einmalig per CASIC-Kommando konfiguriert
 * (Updaterate + reduzierte NMEA-Ausgabe), damit die höhere Rate zuverlässig
 * durch die 9600-Baud-Leitung passt.
 */
#include "gps_speed.h"
#include <SoftwareSerial.h>   // EspSoftwareSerial
#include <TinyGPS++.h>

// ── Konfiguration ──────────────────────────────────────────────────────────
#define GPS_RX_PIN     27     // GPS-TX -> ESP32 GPIO 27 (PWM-Pin 4)
#define GPS_TX_PIN     14     // ESP32 -> GPS-RX  GPIO 14 (PWM-Pin 3)
#define GPS_BAUD       9600   // NMEA-Baudrate
#define GPS_UPDATE_MS  200    // Positions-Updateintervall: 200=5Hz, 100=10Hz, 500=2Hz, 1000=1Hz
#define GPS_CFG_DELAY  800    // ms nach Init warten, bevor die Konfig gesendet wird

// ── interner Zustand ───────────────────────────────────────────────────────
static SoftwareSerial gpsSerial;
static TinyGPSPlus     gps;

static float    speedKmh    = 0.0f;
static uint8_t  sats        = 0;
static bool     fixValid    = false;
static int32_t  latE7       = 0;
static int32_t  lonE7       = 0;
static uint16_t headingCdeg = 0;
static uint16_t altMeters   = 0;

static unsigned long initMs  = 0;
static bool          cfgSent = false;

// ── NMEA-Satz mit korrekter Prüfsumme senden ($<body>*CS\r\n) ──────────────
static void gpsSendNmea(const char* body) {
    uint8_t cs = 0;
    for (const char* p = body; *p; ++p) cs ^= (uint8_t)*p;
    char line[96];
    snprintf(line, sizeof(line), "$%s*%02X\r\n", body, cs);
    gpsSerial.print(line);
}

// ── einmalige GPS-Konfiguration (ATGM336H / AT6558, CASIC) ─────────────────
static void gpsSendConfig() {
    // Updaterate setzen (PCAS02, Intervall in ms)
    char rate[24];
    snprintf(rate, sizeof(rate), "PCAS02,%d", GPS_UPDATE_MS);
    gpsSendNmea(rate);
    // Nur GGA + RMC ausgeben (GGA=Position/Höhe/Sats, RMC=Speed/Kurs) – spart
    // Bandbreite, damit die höhere Rate bei 9600 Baud sicher durchpasst.
    gpsSendNmea("PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0");
}

// ── Initialisierung ────────────────────────────────────────────────────────
void gpsSpeedInit() {
    // Voll-Duplex-Software-Serial: RX=GPIO27 (lesen), TX=GPIO14 (konfigurieren).
    // S.Port (UART1, 32/33) und CRSF (UART2, 16/17) bleiben unberührt.
    gpsSerial.begin(GPS_BAUD, SWSERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    initMs  = millis();
    cfgSent = false;
    Serial.printf("[GPS] Init: RX=GPIO%d TX=GPIO%d  %d Baud\n", GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD);
}

// ── zyklisches Einlesen/Auswerten ──────────────────────────────────────────
void gpsSpeedUpdate() {
    // Konfiguration einmalig kurz nach dem Boot senden (GPS ist dann bereit,
    // und ein evtl. Boot-Glitch auf GPIO14 ist längst vorbei).
    if (!cfgSent && (millis() - initMs) > GPS_CFG_DELAY) {
        gpsSendConfig();
        cfgSent = true;
        Serial.printf("[GPS] Konfig gesendet: %d ms Intervall (%d Hz), GGA+RMC\n",
                      GPS_UPDATE_MS, 1000 / GPS_UPDATE_MS);
    }

    while (gpsSerial.available() > 0) {
        gps.encode((char)gpsSerial.read());
    }

    if (gps.satellites.isValid()) sats = (uint8_t)gps.satellites.value();
    fixValid = gps.location.isValid();

    if (gps.speed.isValid()) {
        speedKmh = (float)gps.speed.kmph();
        if (speedKmh < 0.0f) speedKmh = 0.0f;
    }
    if (gps.location.isValid()) {
        latE7 = (int32_t)(gps.location.lat() * 1e7);
        lonE7 = (int32_t)(gps.location.lng() * 1e7);
    }
    if (gps.course.isValid()) {
        headingCdeg = (uint16_t)(gps.course.deg() * 100.0);
    }
    if (gps.altitude.isValid()) {
        double a = gps.altitude.meters();
        if (a < 0) a = 0;
        altMeters = (uint16_t)a;
    }
}

// ── Abfragefunktionen ──────────────────────────────────────────────────────
float    gpsGetSpeedKmh()    { return speedKmh; }
uint8_t  gpsGetSatellites()  { return sats; }
bool     gpsHasFix()         { return fixValid; }
int32_t  gpsGetLatE7()       { return latE7; }
int32_t  gpsGetLonE7()       { return lonE7; }
uint16_t gpsGetHeadingCdeg() { return headingCdeg; }
uint16_t gpsGetAltMeters()   { return altMeters; }
