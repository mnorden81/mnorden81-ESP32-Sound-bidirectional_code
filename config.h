#ifndef CONFIG_H
#define CONFIG_H

/*
 * config.h  -  ESP32-RC-Sound
 *
 * Hardware_Config:
 *   0 = V1  GPIO-Pins {22, 0, 2, 4}  – BUS + PWM + Pin + Einkanal
 *   1 = V2  GPIO-Pins {14,27,32,33}  – BUS + PWM + Pin + Einkanal
 *   2 = V3  Kein GPIO-Eingang        – nur BUS + Einkanal
 *
 * Das "Port-Modell": auf Hardware V3 gibt es zwei unabhaengig
 * konfigurierbare Zusatz-Ports:
 *   Port A = RC-Eingang, fest (CRSF oder SBUS-Familie, GPIO16 + 17/27)
 *   Port B = echter zweiter Hardware-UART auf GPIO32/33, Rolle waehlbar
 *            (siehe PortBMode weiter unten): Aus, S.Port-Master
 *            (LiPo-Telemetrie an FrSky-Empfaenger), ein zweites
 *            CRSF-faehiges Geraet, Hott-Telemetrie (Menue vorhanden,
 *            Treiber folgt) oder ein generischer Raw-UART mit frei
 *            waehlbarer Baudrate fuer eigene Geraete.
 *   Port C = GPS (SoftwareSerial, GPIO27/14), per PortC_GPS_Enabled
 *            unabhaengig ein-/ausschaltbar. Kollidiert mit SBUS als
 *            Bus-Protokoll (beide belegen GPIO27) - siehe gpsPinConflict
 *            im .ino.
 * Migration: ein NVS-Stand mit Hardware_Config 3 oder 4 wird beim Laden
 * auf 2 (V3) begrenzt. PortB_Mode/PortC_GPS_Enabled starten dabei auf
 * "Aus", siehe loadConfig() in config.cpp.
 *
 * WLAN kann zusaetzlich zum GPIO13-Boot-Schalter zur Laufzeit
 * ein-/ausgeschaltet werden (manuell oder automatisch bei RC-
 * Signalverlust, siehe WifiAutoEnable/WifiAutoTimeoutSec unten).
 */

#include <Arduino.h>

// Rollen fuer Port B (siehe Erklaerung oben). PORTB_HOTT ist im Menue/in
// der Konfiguration waehlbar, aber funktional noch ein Platzhalter - es
// wird noch keine Hott-Telemetrie gesendet/empfangen (kein Treiber
// vorhanden, anders als bei CRSF und S.Port).
enum PortBMode {
    PORTB_OFF   = 0,
    PORTB_CRSF  = 1,
    PORTB_SPORT = 2,
    PORTB_HOTT  = 3,   // in Vorbereitung
    PORTB_RAW   = 4
};
// Anzahl Eintraege im PortBMode-Enum oben - siehe port_function.cpp (Rollen-
// Registry: Web-UI und CRSF/Lua-Menue lesen ihre Optionsliste von dort statt
// sie zusaetzlich hier von Hand zu duplizieren).
static constexpr uint8_t PORTB_FUNCTION_COUNT = 5;
// Protokolltypische Standard-Baudraten - Web-UI/CRSF-Menue setzen die
// Baudrate beim Wechsel des Modus auf diesen Wert vor, sie bleibt aber immer
// frei ueberschreibbar (bei PORTB_RAW gibt es keinen sinnvollen Standard).
static constexpr uint32_t PORTB_BAUD_DEFAULT_CRSF  = 420000;
static constexpr uint32_t PORTB_BAUD_DEFAULT_SPORT = 57600;
static constexpr uint32_t PORTB_BAUD_DEFAULT_HOTT  = 19200;
static constexpr uint32_t PORTB_BAUD_DEFAULT_RAW   = 115200;

// "Mehrfachadress-Erweiterung": statt einer einzelnen Adresse mit
// 2-Bit/Schalter-"Ebenen" werden bis zu RCSOUND_NUM_GROUPS unabhaengige
// CRSF-Gruppenadressen ausgewertet, jede mit den vollen 8 Einzelschaltern
// (1 Bit/Schalter = an/aus, kein Stufenkonzept) - passend zu dem, was das
// WM-Widget (lvglMultiSw) tatsaechlich senden kann. Siehe README_V5. Es
// bleiben die eigenstaendige Einzelkanal-Auswertung (Quelltyp 5, "EK"
// 70-77, ueber die Modul-eigene WM-Adresse modul_adress) sowie 2 frei
// konfigurierbare Gruppenadressen fuer "MKan" (Quelltyp 6, 80-95, je 8
// Kanaele = 16 unabhaengige Sound-Trigger). lib/sound_slots/sound_slots.h
// und test/test_sound_slots/ (Artefakte eines fruaeheren, zurueckgebauten
// Versuchs, auf 32 Sound-Slots zu erweitern) bleiben unangetastet liegen,
// sind aber nicht Teil des esp32dev-Builds.
// Motor(0) + 24 Zusatzsounds = 25 Slots.
static constexpr int RCSOUND_NUM_SLOTS = 25;

// Anzahl der unabhaengig konfigurierbaren CRSF-Gruppenadressen fuer die
// 16 Einzelkanaele (Quelltyp 6, "MKan" 80-95). Jede Gruppe liefert 8
// Kanaele. Adresse 255 = Gruppe deaktiviert (wird nicht ausgewertet).
// Die NVS-Speicherung nutzt je Gruppe einen eigenen Preferences-Key
// ("gadr0", "gadr1", ... siehe config.cpp), keinen Groessen-abhaengigen
// Blob wie bei RCSOUND_NUM_SLOTS - eine Aenderung dieser Konstante ist
// daher unkritisch, ein verwaister Key im NVS wird beim Laden einfach
// ignoriert.
static constexpr int RCSOUND_NUM_GROUPS = 2;

struct ConfigData {
    int Source_Start_Sound[RCSOUND_NUM_SLOTS];   // Quelle Motor(0) + Sound 1-24
    int Volumen_Sound[RCSOUND_NUM_SLOTS];        // Lautstärke 0-200
    int Mode_Sound[RCSOUND_NUM_SLOTS];           // 0=Normal, 1=Loop, 2=Tippbetrieb

    int Source_Speed_Sound_0;    // Quelle Motorgeschwindigkeit
    int throttle_mode;           // 0=Eine Richtung, 1=Zwei Richtungen
    int Min_Speed_Sound_0;       // Mindestgeschwindigkeit Motor (%)
    int Max_Speed_Sound_0;       // Maximalgeschwindigkeit Motor (%)
    int shutdowndelay;           // Motor aus Standgas Verzögerung (s)
    int engine_on_toggle;        // 0=Normal, 1=Toggle
    int throttle_ramp;           // Gas-Rampe (%/s)
    int throttle_dead_band;      // Standgas-Totband (%)

    int Einkanal_Channel;        // BUS-Kanal für Einkanal (999=deaktiviert)
    int Einkanal_mode;           // 0=Normal, 10-13=WM-Adresse 0-3
    int Einkanal_RC_System;      // 0=FrSky,1=FlySky,2=ELRS SBUS,3=Hott,4=ELRS CRSF
    int modul_adress;            // Modul-Adresse (WM/CRSF) - eigene CRSF-Busadresse des Moduls

    // Die CRSF-Gruppenadressen fuer die Einzelkanaele (Quelltyp "MKan",
    // 80-95). UNABHAENGIG von modul_adress - frei einstellbar, damit z.B.
    // mehrere Instanzen des WM-Widgets (je 8 Taster, je eigene
    // "Address"-Option) auf selbstgewaehlte Gruppenadressen zeigen
    // koennen. 255 = Gruppe deaktiviert.
    int EK_Gruppen_Adresse[RCSOUND_NUM_GROUPS];

    // SBUS-Pendant zu EK_Gruppen_Adresse: SBUS kennt (anders als CRSF)
    // keine adressierten Pakete, daher braucht jede zusaetzliche "MKan"-
    // Gruppe hier einen eigenen, fest zugewiesenen physischen SBUS-Kanal
    // statt einer frei waehlbaren Busadresse - siehe
    // einkanalFunctionSBUSGroup() in ESP32-RC-Sound.ino.
    int SBUS_Gruppen_Channel[RCSOUND_NUM_GROUPS]; // 0-15 = SBUS-Kanal, 999 = Gruppe aus
    // Gleiche Werte-/Bedeutungskonvention wie Einkanal_mode (siehe unten):
    // 0 = Normal (Kanalwert direkt, kein WM-Protokoll), 10-13 = WM Adr 0-3
    // (amplituden-codiert, wie beim einzelnen Einkanal-Kanal). Absichtlich
    // kein constrain(0,3) beim Laden/Schreiben, genau wie bei Einkanal_mode.
    int SBUS_Gruppen_Mode[RCSOUND_NUM_GROUPS];

    // Port-Modell - siehe ausfuehrliche Erklaerung am Dateianfang.
    int      PortB_Mode;        // siehe enum PortBMode - Default PORTB_OFF (0)
    uint32_t PortB_Baud;        // Baudrate Port B, frei waehlbar (Vorbelegung je nach Modus)
    int      PortC_GPS_Enabled; // 0=Aus,1=An - ersetzt die alte Kopplung an Hardware_Config==4

    uint8_t sport_poll_id[2];    // S.Port Physical Poll-ID Sensor 1+2 (z.B. 0xA1, 0x22)

    // Diese zwei Felder stammen aus einem nie fertig implementierten
    // Entwurf fuer "Ebenenumschaltung" (2 dedizierte Kanaele: Um-Kanal
    // waehlt eine von mehreren Ebenen, Kanal liefert den Wert darin). Es
    // gibt dafuer keinen Auswerte-Code (weder in Config() noch sonstwo) -
    // die Felder werden nur geladen/gespeichert, aber nirgends gelesen.
    // Die "MKan"-Mehrfachadress-Erweiterung (Quelltyp 6, Bereich 80-95)
    // ist ein ANDERER, hier tatsaechlich implementierter Mechanismus
    // (2-Bit-Multiswitch-Aufloesung statt eigener Kanaele) und nutzt
    // bewusst denselben schon reservierten Namen/Zahlenraum weiter. Diese
    // beiden Felder bleiben unangetastet und weiterhin ungenutzt.
    int Source_Ebenen_Um_Kanal;  // Ebenen Umschalt-Kanal (999=deaktiviert) - reserviert, ungenutzt
    int Source_Ebenen_Kanal;     // Ebenen Werte-Kanal    (999=deaktiviert) - reserviert, ungenutzt

    int Hardware_Config;         // 0=V1, 1=V2, 2=V3 (siehe Port-Modell am
                                  // Dateianfang) - AKTIV LAUFENDER Wert, wird
                                  // nur beim Boot in loadConfig() neu gesetzt.
    // Ein Hardware-Wechsel im laufenden Betrieb aendert NICHT direkt
    // Hardware_Config (das lesen Config()/setup() live), sondern nur diesen
    // "Wunsch"-Wert. Er wird erst beim naechsten Neustart in loadConfig()
    // uebernommen - so bleiben UART/GPS/S.Port waehrend des laufenden
    // Betriebs konsistent auf dem noch aktiven Hardware-Stand.
    int Hardware_Config_Pending;
    int PWM_scale_min;           // PWM Skalierung min (µs) – V1/V2
    int PWM_scale_max;           // PWM Skalierung max (µs) – V1/V2

    char WiFi_SSID[32];
    char WiFi_Password[64];
    char WiFi_IP[16];
    char Device_Name[24];

    // WLAN automatisch aktivieren, wenn ueber die eingestellte Zeit kein
    // gueltiges RC-Signal anliegt (nutzt das bereits vorhandene BUS_OK,
    // siehe wifiFailsafeCheck() in ESP32-RC-Sound.ino) - z.B. wenn kein
    // Sender gebunden ist oder der Empfaenger fehlt, damit das Modul trotzdem
    // per WLAN erreichbar bleibt. Default AUS (opt-in). Getrennt davon
    // (NICHT in dieser Struktur, da bewusst nicht persistiert): der manuelle
    // "WLAN jetzt ein/aus"-Schalter (Web-Tab "WiFi"/Lua-Feld 174) bildet nur
    // den aktuellen Laufzeit-Zustand ab, siehe WebServerManager::
    // enableAP()/disableAP().
    bool     WifiAutoEnable;
    uint16_t WifiAutoTimeoutSec;   // 5-240s, Default 60

    // WLAN dauerhaft aktiv: der AP wird in setup() fest eingeschaltet und
    // danach von der Firmware nie mehr ausgeschaltet - GPIO13-Bootpin (siehe
    // setup()), der manuelle Schalter (CRSF/Lua-Feld 174) und der
    // Auto-Failsafe oben (WifiAutoEnable/WifiAutoTimeoutSec, Felder 175/176)
    // werden dabei uebersprungen, siehe wifiFailsafeCheck() in
    // ESP32-RC-Sound.ino. Default AN (true) - bewusst auch fuer
    // Bestandsgeraete beim ersten Boot nach einem Firmware-Update, siehe
    // Migrations-Default in loadConfig() (dort: p.getInt("wfalways", 1)).
    bool     WifiAlwaysOn;
};

extern ConfigData    config;
extern bool          configDirty;
extern unsigned long configDirtyMs;

void loadConfig();
void saveConfig();
void saveConfigForce();
void markDirty();
void Reset_all();
void set_sbus();
void set_pwm();
void set_pin();
// Fortlaufende, ueber Neustarts hinweg persistente Nummer fuer
// "Konfiguration auf SD sichern" (siehe WebServerManager.cpp) - eigener
// Preferences-Key (kein Groessen-abhaengiger Blob), daher unkritisch wie
// EK_Gruppen_Adresse. Jeder Aufruf erhoeht den gespeicherten Wert um 1 und
// gibt den neuen Wert zurueck, damit Backup-Dateinamen nie kollidieren.
uint32_t nextSdBackupCounter();
// True, solange Hardware_Config_Pending vom aktiv laufenden
// Hardware_Config abweicht - der neue Wert wirkt erst nach einem Neustart.
static inline bool hardwareConfigRestartPending() {
    return config.Hardware_Config_Pending != config.Hardware_Config;
}

#endif
