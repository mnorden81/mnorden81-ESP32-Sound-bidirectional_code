/*
 * gps_speed.h  –  ESP32-RC-Sound  (Port C - per PortC_GPS_Enabled
 * unabhaengig schaltbar, siehe Port-Modell in config.h)
 *
 * GPS-Geschwindigkeitsmessung, nur auf Hardware V3 nutzbar (dort sind die
 * Pins frei - siehe config.h).
 * Liest ein NMEA-GPS-Modul (z. B. u-blox NEO-M8N oder ATGM336H) über eine
 * Software-Serial ein und stellt Ground Speed sowie Positionsdaten bereit.
 *
 * Pinbelegung:
 *   GPS-TX  -> GPIO 27  (PWM-Pin 4, Empfang der NMEA-Daten)
 *   GPS-RX  <- GPIO 14  (PWM-Pin 3, Senden der Konfiguration an das GPS)
 *
 * S.Port (UART1, GPIO 32/33) und CRSF (UART2, GPIO 16/17) bleiben unberührt.
 *
 * Nach dem Start wird das GPS einmalig per CASIC-Kommando auf 5 Hz gestellt
 * (ATGM336H/AT6558) und auf die Ausgabe von GGA+RMC reduziert. Die Einstellung
 * ist flüchtig und wird bei jedem Start neu gesetzt.
 *
 * Benötigte Bibliotheken: EspSoftwareSerial (SoftwareSerial.h), TinyGPSPlus.
 */
#ifndef GPS_SPEED_H
#define GPS_SPEED_H

#include <Arduino.h>

void     gpsSpeedInit();       // GPS initialisieren (RX=GPIO27, TX=GPIO14)
void     gpsSpeedUpdate();     // NMEA einlesen/auswerten + Konfig nach Boot senden

float    gpsGetSpeedKmh();     // aktuelle Geschwindigkeit in km/h
uint8_t  gpsGetSatellites();   // Anzahl Satelliten im Fix
bool     gpsHasFix();          // gültiger Positions-Fix?

int32_t  gpsGetLatE7();        // Breitengrad * 1e7   (für CRSF-GPS-Frame)
int32_t  gpsGetLonE7();        // Längengrad * 1e7
uint16_t gpsGetHeadingCdeg();  // Kurs über Grund * 100
uint16_t gpsGetAltMeters();    // Höhe in Metern (Frame addiert +1000 selbst)

#endif // GPS_SPEED_H
