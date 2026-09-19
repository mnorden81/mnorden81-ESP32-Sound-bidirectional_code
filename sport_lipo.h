#ifndef SPORT_LIPO_H
#define SPORT_LIPO_H

/*
 * sport_lipo.h  –  ESP32-RC-Sound  (Port B, Rolle PORTB_SPORT - ab v6.0
 * unabhaengig waehlbar statt fest an "Hardware V4" gekoppelt, siehe
 * Port-Modell in config.h)
 *
 * S.Port Master-Polling fuer bis zu 2 FrSky FLVSS/MLVSS LiPo-Sensoren.
 * Der ESP32 übernimmt die Rolle des S.Port-Masters und pollt die Sensoren
 * aktiv ueber einen dedizierten Hardware-UART (dynamisch: Serial2 bei SBUS, Serial1 bei CRSF).
 *
 * Port B – Pins (nur auf Hardware V3 frei, siehe config.h):
 *   GPIO 33  → S.Port TX  (Output, kein Strapping)
 *   GPIO 32  → S.Port RX  (Input, kein Strapping)
 *
 * Schaltung (Half-Duplex, S.Port ist invertierter UART 57600 Baud):
 *
 *   ESP32 GPIO 33 (TX) ──[1N4148 Anode→Kathode]──┬── S.Port Signal (gelb)
 *   ESP32 GPIO 32 (RX) ────────────────────────────┘
 *   ESP32 GND ──────────────────────────────────────── S.Port GND (schwarz)
 *
 *   Diode 1N4148: Anode an GPIO 33, Kathode Richtung Bus
 *   (schwarzer Ring zeigt WEG von GPIO 33, Richtung S.Port Kabel)
 *
 * Warum GPIO 32 / 33?
 *   In V1: GPIO 32/33 komplett frei (V1 nutzt GPIO 0,2,4,16,17,22)
 *   In V2: GPIO 32 = PWM 5, GPIO 33 = PWM 6 – auf Port B (nur V3) nicht relevant
 *   In V3: beide frei (V3 hat keine PWM-Eingänge)
 *   Kein Strapping-Pin, kein Boot-Konflikt, vollwertig Input+Output
 *   SBUS-Betrieb: Serial1=SBUS -> S.Port auf Serial2
 *   CRSF-Betrieb: Serial2=CRSF -> S.Port auf Serial1
 *
 * Sensor Poll-IDs (konfigurierbar über Weboberfläche Seite 12):
 *   Sensor 1 (Pack 1): Physical ID 1 (EdgeTX) → Poll-Byte 0xA1
 *   Sensor 2 (Pack 2): Physical ID 2 (EdgeTX) → Poll-Byte 0x22
 *
 * ⚠ 12S Hinweis: Bei 2×6S in Reihe darf GND NUR am ersten Sensor
 *   angeschlossen werden. Am zweiten Sensor entsteht sonst ein Kurzschluss!
 */

#include <Arduino.h>

// ── Pin-Konfiguration Port B ────────────────────────────────────────────
// Nur noch Referenz/Dokumentation - sportLipoInit() bekommt rxPin/txPin seit
// der Port-B-Rollen-Registry (siehe port_function.h) als Parameter, physisch
// sind es aber weiterhin immer diese beiden Pins (Port B ist fest verdrahtet).
#define SPORT_TX_PIN     33      // Output GPIO, kein Strapping
#define SPORT_RX_PIN     32      // Input GPIO, kein Strapping

// S.Port: 57600 Baud, 8N1, invertiert
#define SPORT_BAUD       57600

// ── Sensor-Konfiguration ──────────────────────────────────────────────
#define SPORT_MAX_CELLS    6     // FLVSS/MLVSS: max. 6S pro Sensor
#define SPORT_NUM_SENSORS  2     // Pack 1 + Pack 2

// Physical Poll-Bytes werden zur Laufzeit aus config.sport_poll_id[] gelesen.
// Standardwerte (Werkseinstellung):
//   Sensor 1: Physical ID 0x02 → Poll-Byte 0xA1
//   Sensor 2: Physical ID 0x03 → Poll-Byte 0x22
// Konfigurierbar über Weboberfläche → Seite 12 (FrSky LiPo Sensoren)

// ── Protokoll-Konstanten ──────────────────────────────────────────────
#define SPORT_START_BYTE   0x7E
#define SPORT_DATA_FRAME   0x10
#define SPORT_CELLS_ID     0x0300   // Data-ID FLVSS Einzelzellen (0x0300–0x030F)
#define SPORT_STUFF_BYTE   0x7D     // Byte-Stuffing Escape
#define SPORT_STUFF_MASK   0x20     // XOR-Maske nach Escape

// ── Timing ────────────────────────────────────────────────────────────
#define SPORT_POLL_MS      30       // Poll-Intervall pro Sensor (ms) – gibt Antwortzeit
#define SPORT_TIMEOUT_MS   2000     // Sensor offline nach dieser Zeit ohne Paket

// ── Diagnose ──────────────────────────────────────────────────────────
// SPORT_DEBUG_RAW = 1 : jede Sekunde RX-Byte-Zähler + Hexdump auf Serial,
// zusaetzlich [CELL]-, [S.Port LIVE]- und Sensor-Offline-Meldungen.
// Fuer Normalbetrieb auf 0. Zum Debuggen auf 1 setzen.
#define SPORT_DEBUG_RAW    0

// SPORT_DEBUG_PROBE = 1 : Diagnose-Sweep (deaktiviert, war fehlerhaft).
#define SPORT_DEBUG_PROBE  0
#define SPORT_PROBE_PHASE_MS 5000   // Dauer pro ID-Phase im Sweep (ms)

// ── Datenstruktur pro Sensor ──────────────────────────────────────────
struct LipoSensorData {
    float    cellVoltage[SPORT_MAX_CELLS];  // Einzelzellen in Volt
    uint8_t  cellCount;                     // Erkannte Zellanzahl (0 = offline)
    float    totalVoltage;                  // Summe aller Zellen in Volt
    float    minCell;                       // Niedrigste Einzelzelle in Volt
    unsigned long lastUpdateMs;             // Zeitstempel letztes gültiges Paket
    bool     online;                        // true = Sensor antwortet
};

// ── Öffentliche Daten ─────────────────────────────────────────────────
extern LipoSensorData lipoSensor[SPORT_NUM_SENSORS];

// ── Öffentliche Funktionen ────────────────────────────────────────────
// Signatur passend zur Port-B-Rollen-Registry (siehe port_function.h):
// serial = von portBFreeSerial() (im .ino) ermittelter freier Hardware-UART,
// rxPin/txPin = physisch immer 32/33, baud = config.PortB_Baud. Die fruehere
// interne Serial1/Serial2-Entscheidung ist damit entfallen - es gibt jetzt
// nur noch EINE Stelle (portBFreeSerial() im .ino), die das festlegt.
void    sportLipoInit(HardwareSerial* serial, uint8_t rxPin, uint8_t txPin, uint32_t baud);
void    sportLipoUpdate();              // Non-blocking, in loop() aufrufen
bool    sportLipoIsActive();            // true = UART initialisiert (S.Port aktiv auf Port B zugewiesen)

float   sportGetTotalVoltage();         // Gesamtspannung beider Packs (V)
float   sportGetMinCell();              // Niedrigste Einzelzelle aller Packs (V)
uint8_t sportGetTotalCells();           // Gesamtzellanzahl beider Packs
uint8_t sportCalcSoC(float minCell);    // SoC aus min. Zellspannung (0–100%)
void    sportSendCellsTelemetry(uint8_t sensorIdx); // CRSF Voltages senden

// ── Diagnose-Zaehler (immer aktiv, nicht nur bei SPORT_DEBUG_RAW) ──────
// Uebernommen aus ESP32-GPS-Telemetry: granulare Zaehler machen "geht
// nicht" auf der Debug-Seite direkt sichtbar (kommen ueberhaupt Bytes an?
// werden Polls beantwortet? passt die CRC?), statt nur online/offline.
uint32_t sportGetRawBytesRx();      // alle je empfangenen Bytes auf dem S.Port-Bus
uint32_t sportGetPollsSent();       // von uns gesendete Polls (0x7E + Poll-ID)
uint32_t sportGetValidFrames();     // erfolgreich dekodierte CELLS-Frames
uint32_t sportGetCrcErrors();       // CRC-Fehler in Sensor-Antworten
uint32_t sportGetNoResponseCount(); // Poll gesendet, aber keine/ungueltige Antwort (kein DATA_FRAME)
uint8_t  sportGetLastPollId();      // zuletzt gesehene Poll-ID (eigene oder fremde Antwort)

#endif // SPORT_LIPO_H
