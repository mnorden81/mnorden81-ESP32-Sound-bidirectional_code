/*
 * sport_lipo.cpp  –  ESP32-RC-Sound  (Port B, Rolle PORTB_SPORT - ab v6.0
 * unabhaengig von der Hardware-Version waehlbar statt fest an "V4" gekoppelt,
 * siehe Port-Modell in config.h)
 *
 * S.Port Master: ESP32 pollt FrSky FLVSS/MLVSS Sensoren und parst
 * Einzelzell-Spannungen. Automatische Erkennung der Zellanzahl (1–6S).
 *
 * S.Port Paketformat (Sensor → Master, nach Byte-Stuffing 8 Bytes):
 *   Byte 0: 0x10 (DATA_FRAME)
 *   Byte 1: Data-ID Low
 *   Byte 2: Data-ID High
 *   Byte 3–6: Payload (32 Bit, Little Endian)
 *   Byte 7: CRC
 *
 * CELLS-Payload (Data-ID 0x0300, 32 Bit):
 *   Bits  3:0  → Zell-Startindex (0 = Paar 1+2, 1 = Paar 3+4, 2 = Paar 5+6)
 *   Bits 14:4  → Spannung Zelle A in Einheiten von 5mV
 *   Bits 17:15 → Gesamtzellanzahl des Packs (1–6), nur in Paket mit Index 0 gültig
 *   Bits 28:18 → Spannung Zelle B in Einheiten von 5mV (0 wenn nicht vorhanden)
 *
 * Beispiel 4S-Pack, erstes Paket (Index 0):
 *   Zelle 1 = 3.810V → 762 × 5mV, Zelle 2 = 3.820V → 764 × 5mV, Count = 4
 *   value = (0) | (762 << 4) | (4 << 15) | (764 << 18)
 */

#include "sport_lipo.h"
#include "crsf_esp32.h"
#include "config.h"

// Zugriff auf den CRSF-Instanz aus dem Hauptsketch
extern CRSF crsf;

// ── Globale Sensor-Daten ──────────────────────────────────────────────
LipoSensorData lipoSensor[SPORT_NUM_SENSORS] = {};

// ── UART-Instanz ──────────────────────────────────────────────────────
// SBUS belegt Serial1, CRSF belegt Serial2.
// S.Port bekommt den jeweils freien UART:
//   SBUS-Betrieb (RC-System != 4) -> S.Port auf Serial2
//   CRSF-Betrieb (RC-System == 4) -> S.Port auf Serial1
static HardwareSerial* sportSerialPtr = nullptr;

// ── State-Machine ─────────────────────────────────────────────────────
// S.Port Sequenz nach einem Poll (Half-Duplex, eigenes Echo sichtbar):
//
//  Master TX:   0x7E  0xA1          (Poll: Start + Physical-ID)
//  Bus Echo:    0x7E  0xA1          (eigenes Echo zurück)
//  Sensor TX:   0x7E  0x10  D0..D6  CRC   (Antwort: Start + DATA_FRAME + 7 Bytes)
//
// Zustaende:
//   WAIT_START  : warte auf 0x7E
//   WAIT_ECHO   : erstes 0x7E gesehen – naechstes Byte ist Physical-ID-Echo (ignorieren)
//   WAIT_FRAME  : Echo konsumiert – warte auf naechstes 0x7E (Sensor-Antwort)
//   WAIT_FTYPE  : 0x7E der Sensor-Antwort gesehen – naechstes Byte muss 0x10 sein
//   RECV_DATA   : sammle 7 Payload-Bytes + 1 CRC-Byte
//
enum SportRxState : uint8_t { WAIT_START, WAIT_ECHO, WAIT_FRAME, WAIT_FTYPE, RECV_DATA };
static SportRxState rxState   = WAIT_START;
static uint8_t      rxBuf[7]  = {};
static uint8_t      rxIdx     = 0;
static bool         rxStuffed = false;

// Welcher Sensor gerade auf Antwort wartet
static uint8_t      pollSensor   = 0;
static unsigned long lastPollMs  = 0;
static uint8_t      lastPollId   = 0;   // Poll-ID der zuletzt gesehenen Antwort

// ── Diagnose-Zaehler (immer aktiv) ─────────────────────────────────────
// Learning aus ESP32-GPS-Telemetry: diese vier Zaehler grenzen "geht
// nicht" sofort ein: 0 Rohbytes -> Verkabelung; Polls gesendet aber immer
// noResponse -> Physical-ID/Sensor nicht angeschlossen; crcErrors hoch ->
// Baudrate/Stoerung; validFrames steigt -> alles ok.
static uint32_t g_sportRawBytesRx      = 0;
static uint32_t g_sportPollsSent       = 0;
static uint32_t g_sportValidFrames     = 0;
static uint32_t g_sportCrcErrors       = 0;
static uint32_t g_sportNoResponseCount = 0;

// Gemeinsame Zeitbasis: wird am Anfang von sportLipoUpdate() gesetzt und in
// sportProcessByte() fuer lastUpdateMs genutzt. So rechnet der Timeout-Check
// mit exakt demselben Zeitstempel -> kein vorzeichenloser Unterlauf moeglich.
static unsigned long sportNow    = 0;

#if SPORT_DEBUG_PROBE
// Globale Zähler für den Diagnose-Sweep (werden in sportProcessByte erhöht)
uint32_t g_probeResp   = 0;
uint8_t  g_probeCells2 = 0;
uint8_t  g_probeCells3 = 0;
#endif

// ── CRC (S.Port: Summe aller Bytes mod 256, dann 0xFF minus Ergebnis) ─
static uint8_t sportCrc(const uint8_t* buf, uint8_t len) {
    uint16_t sum = 0;
    for (uint8_t i = 0; i < len; i++) sum += buf[i];
    sum = (sum >> 8) + (sum & 0xFF);
    return (uint8_t)(0xFF - sum);
}

// ── Empfangenes Byte verarbeiten ──────────────────────────────────────
static void sportProcessByte(uint8_t b) {

    // 0x7E ist immer Frame-Start
    if (b == SPORT_START_BYTE) {
        // Jedes 0x7E startet einen neuen Frame. Danach kommt die Poll-ID,
        // dann DIREKT das DATA_FRAME (0x10) + Daten – KEIN zweites 0x7E.
        rxState   = WAIT_ECHO;   // naechstes Byte = Poll-ID (Echo)
        rxIdx     = 0;
        rxStuffed = false;
        return;
    }

    switch (rxState) {

        case WAIT_ECHO:
            // Poll-ID (z.B. 0xA1 / 0x22). Merken, welcher Sensor antwortet,
            // dann direkt auf das DATA_FRAME-Byte warten.
            lastPollId = b;
            rxState    = WAIT_FTYPE;
            break;

        case WAIT_FTYPE:
            // Muss 0x10 (DATA_FRAME) sein. Alles andere (z.B. 0x00 = kein
            // Sensor / Leerantwort) verwerfen und auf naechstes 0x7E warten.
            if (b != SPORT_DATA_FRAME) {
                g_sportNoResponseCount++;
                rxState = WAIT_START;
                break;
            }
            rxState = RECV_DATA;
            rxIdx   = 0;
            break;

        case RECV_DATA:
            // Byte-Stuffing auflösen (nur in Nutzlast, nicht auf 0x7E)
            if (!rxStuffed && b == SPORT_STUFF_BYTE) {
                rxStuffed = true;
                break;
            }
            if (rxStuffed) {
                b ^= SPORT_STUFF_MASK;
                rxStuffed = false;
            }

            if (rxIdx < 7) rxBuf[rxIdx++] = b;

            if (rxIdx == 7) {
                rxState = WAIT_START;

                // rxBuf layout (DATA_FRAME bereits in WAIT_FTYPE konsumiert):
                //   [0..1] = Data-ID (LE)
                //   [2..5] = Payload (LE)
                //   [6]    = CRC
                // S.Port CRC: Summe aller Bytes nach 0x7E: 0x10 + DataID[0..1] + Payload[0..3]
                uint16_t crcSum = SPORT_DATA_FRAME;
                for (uint8_t i = 0; i < 6; i++) crcSum += rxBuf[i];
                crcSum = (crcSum >> 8) + (crcSum & 0xFF);
                uint8_t expectedCrc = (uint8_t)(0xFF - crcSum);
                if (expectedCrc != rxBuf[6]) { g_sportCrcErrors++; break; }

                // ── Data-ID (16 Bit, LE) ──────────────────────────────
                uint16_t dataId = (uint16_t)rxBuf[0] | ((uint16_t)rxBuf[1] << 8);
                if (dataId < SPORT_CELLS_ID || dataId > (SPORT_CELLS_ID + 0x0F)) break;

                // ── Payload (32 Bit, LE) ──────────────────────────────
                uint32_t val = (uint32_t)rxBuf[2]
                             | ((uint32_t)rxBuf[3] << 8)
                             | ((uint32_t)rxBuf[4] << 16)
                             | ((uint32_t)rxBuf[5] << 24);

                // ── CELLS dekodieren (FrSky FLVSS / pawelsky-Format) ───
                //   Bits  0-3  : firstCellNo (Index des ersten Zellpaars)
                //   Bits  4-7  : cellNum     (Gesamtzahl Zellen im Pack)
                //   Bits  8-19 : Zelle (firstCellNo)   * 500   (12 Bit)
                //   Bits 20-31 : Zelle (firstCellNo+1) * 500   (12 Bit)
                uint8_t firstCell  = (uint8_t)(val & 0x0F);
                uint8_t totalCells = (uint8_t)((val >> 4) & 0x0F);
                float   voltA      = ((val >> 8)  & 0xFFF) / 500.0f;
                float   voltB      = ((val >> 20) & 0xFFF) / 500.0f;

                // Leerantwort (cellNum==0) ignorieren – ist kein gueltiger Pack
                if (totalCells == 0 || totalCells > SPORT_MAX_CELLS) break;

                // ── Sensor anhand der Poll-ID zuordnen ────────────────
                // A1 -> Sensor 0 (Pack 1), 22 -> Sensor 1 (Pack 2).
                // Faellt auf pollSensor zurueck, falls ID unbekannt.
                uint8_t sIdx = pollSensor;
                if (lastPollId == config.sport_poll_id[0])      sIdx = 0;
                else if (lastPollId == config.sport_poll_id[1]) sIdx = 1;
                if (sIdx >= SPORT_NUM_SENSORS) sIdx = 0;

#if SPORT_DEBUG_RAW
                Serial.printf("[CELL] pollID=0x%02X -> Sensor%d  cells=%d firstCell=%d  vA=%.3f vB=%.3f\n",
                              lastPollId, sIdx + 1, totalCells, firstCell, voltA, voltB);
#endif

                LipoSensorData& s = lipoSensor[sIdx];
                s.cellCount = totalCells;

                // Spannungen an die richtigen Positionen schreiben
                if (firstCell < SPORT_MAX_CELLS && voltA > 0.01f)
                    s.cellVoltage[firstCell] = voltA;
                if ((firstCell + 1) < SPORT_MAX_CELLS && voltB > 0.01f)
                    s.cellVoltage[firstCell + 1] = voltB;

                // Gesamt- und Minimalspannung neu berechnen (bekannte Zellen)
                s.totalVoltage = 0.0f;
                s.minCell      = 9.9f;
                for (uint8_t i = 0; i < s.cellCount; i++) {
                    s.totalVoltage += s.cellVoltage[i];
                    if (s.cellVoltage[i] > 0.1f && s.cellVoltage[i] < s.minCell)
                        s.minCell = s.cellVoltage[i];
                }
                if (s.minCell > 9.0f) s.minCell = 0.0f;
                // Gemeinsame Zeitbasis sportNow nutzen (vom Timeout-Check geteilt),
                // damit (now - lastUpdateMs) niemals vorzeichenlos unterlaeuft und
                // der Sensor nicht faelschlich sofort offline geht.
                s.lastUpdateMs = sportNow;
                s.online       = true;
                g_sportValidFrames++;
            }
            break;

        default:
            rxState = WAIT_START;
            break;
    }
}

// ── Initialisierung ───────────────────────────────────────────────────
// serial/rxPin/txPin/baud kommen jetzt als Parameter von der Port-B-Rollen-
// Registry (siehe port_function.cpp) - welcher Hardware-UART frei ist,
// entscheidet ausschliesslich portBFreeSerial() im .ino (einzige Stelle,
// vorher war dieselbe SBUS/CRSF-Fallunterscheidung hier UND im .ino
// dupliziert).
void sportLipoInit(HardwareSerial* serial, uint8_t rxPin, uint8_t txPin, uint32_t baud) {
    sportSerialPtr = serial;
    sportSerialPtr->begin(baud, SERIAL_8N1, rxPin, txPin, true);

    // Sensor-Strukturen zurücksetzen
    for (uint8_t i = 0; i < SPORT_NUM_SENSORS; i++) {
        memset(&lipoSensor[i], 0, sizeof(LipoSensorData));
    }

    rxState    = WAIT_START;
    pollSensor = 0;
    lastPollMs = 0;

    Serial.printf("[S.Port] Init: TX=GPIO%d  RX=GPIO%d  %u Baud (invertiert, UART%d)\n",
                  txPin, rxPin, (unsigned)baud,
                  (config.Einkanal_RC_System == 4) ? 1 : 2);
}

// isActive(): true, sobald sportLipoInit() gelaufen ist (Registry-Getter,
// siehe port_function.h PortBFunctionModule::isActive).
bool sportLipoIsActive() { return sportSerialPtr != nullptr; }

// ── Update (non-blocking, in loop() aufrufen) ─────────────────────────
void sportLipoUpdate() {
    // Schutz: Wird Hardware_Config zur Laufzeit auf V4 gestellt (per Web/API), ist
    // die S.Port-UART noch nicht initialisiert (sportLipoInit() laeuft nur in setup()).
    // Ohne diesen Check gaebe es einen Null-Pointer-Zugriff (Guru Meditation).
    // S.Port arbeitet dann erst nach einem Neustart – der Crash bleibt aber aus.
    if (sportSerialPtr == nullptr) return;
    unsigned long now = millis();
    sportNow = now;   // gemeinsame Zeitbasis fuer sportProcessByte() (lastUpdateMs)

#if SPORT_DEBUG_RAW
    // Diagnose-Lebenszeichen: zaehlt Aufrufe pro Sekunde (nur im Debug-Build).
    static unsigned long lastAlive = 0;
    static uint32_t      callCount = 0;
    callCount++;
    if (now - lastAlive >= 1000) {
        lastAlive = now;
        bool ptrOk = (sportSerialPtr != nullptr);
        uint32_t avail = ptrOk ? (uint32_t)sportSerialPtr->available() : 0;
        Serial.printf("[S.Port LIVE] Updates/s=%lu  ptrOk=%d  avail=%lu\n",
                      (unsigned long)callCount, ptrOk, (unsigned long)avail);
        callCount = 0;
    }
#endif

#if SPORT_DEBUG_RAW
    static unsigned long lastDbg = 0;
    static uint32_t      rxByteCount = 0;
    static uint8_t       dbgLine[32];
    static uint8_t       dbgIdx = 0;
#endif

    // Empfangene Bytes verarbeiten
    while (sportSerialPtr->available()) {
        uint8_t bb = (uint8_t)sportSerialPtr->read();
        g_sportRawBytesRx++;
#if SPORT_DEBUG_RAW
        rxByteCount++;
        if (dbgIdx < sizeof(dbgLine)) dbgLine[dbgIdx++] = bb;
#endif
        sportProcessByte(bb);
    }

#if SPORT_DEBUG_RAW
    // Alle 1000 ms: Anzahl RX-Bytes + die letzten empfangenen Bytes als Hex ausgeben
    if (now - lastDbg >= 1000) {
        lastDbg = now;
        Serial.printf("[S.Port DBG] RX-Bytes/s: %lu  | letzte: ", (unsigned long)rxByteCount);
        for (uint8_t i = 0; i < dbgIdx; i++) Serial.printf("%02X ", dbgLine[i]);
        Serial.printf(" | Pack1.online=%d cells=%d\n",
                      lipoSensor[0].online, lipoSensor[0].cellCount);
        rxByteCount = 0;
        dbgIdx      = 0;
    }
#endif

    // Sensor-Timeout prüfen
    for (uint8_t i = 0; i < SPORT_NUM_SENSORS; i++) {
        if (lipoSensor[i].online &&
            (now - lipoSensor[i].lastUpdateMs) > SPORT_TIMEOUT_MS) {
            lipoSensor[i].online     = false;
            lipoSensor[i].cellCount  = 0;
            lipoSensor[i].totalVoltage = 0.0f;
            lipoSensor[i].minCell    = 0.0f;
#if SPORT_DEBUG_RAW
            Serial.printf("[S.Port] Sensor %d offline\n", i + 1);
#endif
        }
    }

    // Anzahl aktiver (unterschiedlicher) Sensoren bestimmen:
    // Sind beide Poll-IDs identisch, ist nur Sensor 1 konfiguriert.
    uint8_t activeSensors = 1;
    if (config.sport_poll_id[1] != config.sport_poll_id[0]) {
        activeSensors = SPORT_NUM_SENSORS;
    }

    // Poll-Zyklus mit garantiertem Antwortfenster:
    //   1. Poll senden, dann SPORT_RESPONSE_MS auf Antwort warten
    //   2. erst danach den nächsten Sensor pollen
    // So wird die Antwort eines Sensors nie durch den nächsten Poll
    // abgeschnitten – das war die Ursache, warum 2 Sensoren gleichzeitig
    // nicht funktionierten (einzeln dagegen schon).
    if ((now - lastPollMs) >= SPORT_POLL_MS) {
        lastPollMs = now;

        // Nächsten Sensor wählen (nur aktive)
        pollSensor = (pollSensor + 1) % activeSensors;

        // 0x7E + Physical-ID senden → Sensor antwortet mit DATA_FRAME
        sportSerialPtr->write(SPORT_START_BYTE);
        sportSerialPtr->write(config.sport_poll_id[pollSensor]);
        g_sportPollsSent++;

        // State-Machine für die erwartete Antwort vorbereiten
        rxState   = WAIT_START;
        rxIdx     = 0;
        rxStuffed = false;
    }
}

// ── Hilfsfunktionen ───────────────────────────────────────────────────

float sportGetTotalVoltage() {
    float v = 0.0f;
    for (uint8_t i = 0; i < SPORT_NUM_SENSORS; i++)
        if (lipoSensor[i].online) v += lipoSensor[i].totalVoltage;
    return v;
}

float sportGetMinCell() {
    float minV  = 9.9f;
    bool  found = false;
    for (uint8_t i = 0; i < SPORT_NUM_SENSORS; i++) {
        if (lipoSensor[i].online && lipoSensor[i].minCell > 0.1f) {
            if (lipoSensor[i].minCell < minV) { minV = lipoSensor[i].minCell; found = true; }
        }
    }
    return found ? minV : 0.0f;
}

uint8_t sportGetTotalCells() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < SPORT_NUM_SENSORS; i++)
        if (lipoSensor[i].online) n += lipoSensor[i].cellCount;
    return n;
}

/*
 * Ladezustand (SoC) aus niedrigster Einzelzellspannung (LiPo-Ruhespannung):
 *   4.20V = 100%   4.00V = 75%   3.80V = 50%
 *   3.60V = 25%    3.40V = 10%   < 3.40V = 0%
 */
uint8_t sportCalcSoC(float minCell) {
    if (minCell <= 0.1f)  return 0;
    if (minCell >= 4.20f) return 100;
    if (minCell >= 4.00f) return (uint8_t)map((long)(minCell * 100), 400, 420,  75, 100);
    if (minCell >= 3.80f) return (uint8_t)map((long)(minCell * 100), 380, 400,  50,  75);
    if (minCell >= 3.60f) return (uint8_t)map((long)(minCell * 100), 360, 380,  25,  50);
    if (minCell >= 3.40f) return (uint8_t)map((long)(minCell * 100), 340, 360,  10,  25);
    return 0;
}

/*
 * Einzelzellen eines Sensors als CRSF Voltages-Telemetrie senden.
 * sensorIdx: 0 = Pack 1 (source_id=1), 1 = Pack 2 (source_id=2)
 * Werte in mV (int32_t). EdgeTX erkennt diese nach "Sensor suchen"
 * als native Sensoren und zeigt sie als Widgets an.
 */
void sportSendCellsTelemetry(uint8_t sensorIdx) {
    if (sensorIdx >= SPORT_NUM_SENSORS) return;
    const LipoSensorData& s = lipoSensor[sensorIdx];
    if (!s.online || s.cellCount == 0) return;

    uint8_t srcId = sensorIdx + 1;

    int32_t mv[SPORT_MAX_CELLS];
    for (uint8_t i = 0; i < s.cellCount; i++)
        mv[i] = (int32_t)(s.cellVoltage[i] * 1000.0f + 0.5f);

    switch (s.cellCount) {
        case 1: crsf.send_tele_Voltages(srcId, {mv[0]}); break;
        case 2: crsf.send_tele_Voltages(srcId, {mv[0], mv[1]}); break;
        case 3: crsf.send_tele_Voltages(srcId, {mv[0], mv[1], mv[2]}); break;
        case 4: crsf.send_tele_Voltages(srcId, {mv[0], mv[1], mv[2], mv[3]}); break;
        case 5: crsf.send_tele_Voltages(srcId, {mv[0], mv[1], mv[2], mv[3], mv[4]}); break;
        case 6: crsf.send_tele_Voltages(srcId, {mv[0], mv[1], mv[2], mv[3], mv[4], mv[5]}); break;
        default: break;
    }
}

// ── Diagnose-Getter (fuer die Web-Debugseite) ──────────────────────────
uint32_t sportGetRawBytesRx()      { return g_sportRawBytesRx; }
uint32_t sportGetPollsSent()       { return g_sportPollsSent; }
uint32_t sportGetValidFrames()     { return g_sportValidFrames; }
uint32_t sportGetCrcErrors()       { return g_sportCrcErrors; }
uint32_t sportGetNoResponseCount() { return g_sportNoResponseCount; }
uint8_t  sportGetLastPollId()      { return lastPollId; }
