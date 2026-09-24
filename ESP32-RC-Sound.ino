/*
   ESP32-RC-Sound - Soundmodul-Firmware fuer RC-Modelle (ESP32)
   PiperPilot

   Eigenstaendiges Projekt. firmware-v5, firmware-v6 und firmware-v7 sind
   drei getrennte, unabhaengig baubare Projektstaende (kein gemeinsamer
   Code-Stand, keine gemeinsame Versionszaehlung).

   ── Basis ────────────────────────────────────────────────────────────────────
   Technische Basis ist Frank Verfuerths Projekt "ESP32-SBUS-Switch" (2022) -
   von dort stammt der grundlegende Ansatz fuer SBUS/CRSF/S.Port-Protokoll-
   implementierung, WLAN-Weboberflaeche zur Konfiguration und I2S-Audio, der
   seither allen hier weiterentwickelten Projekten zugrunde liegt. Das
   Soundmodul selbst (Hardware wie Firmware) ist daraus als eigenstaendiges,
   neu entworfenes Projekt hervorgegangen (Repository urspruenglich unter
   github.com/Ziege-One/ESP32-RC-Sound, seither unter dem Autorennamen
   PiperPilot fortgefuehrt).

   ── Ueberblick ────────────────────────────────────────────────────────────────
   Das Modul spielt WAV-Sounds (ein Motorsound-Zyklus start/loop/shut plus
   bis zu 24 frei zuordenbare Zusatzsounds) von einer SD-Karte ueber I2S
   ab, gesteuert ueber den RC-Bus (SBUS oder CRSF/ELRS) oder - bei V1/V2-
   Hardware zusaetzlich - ueber direkte PWM-/GPIO-Eingaenge. Konfiguriert
   wird es wahlweise per Weboberflaeche (WLAN) oder direkt am Sender ueber
   das native CRSF-/Lua-Parametermenue.

   ── Hardware-Varianten ───────────────────────────────────────────────────────────
   V1  GPIO-Pins {22, 0, 2, 4}   – BUS + PWM-Eingang + GPIO-Pin + Einkanal
   V2  GPIO-Pins {14,27,32,33}   – BUS + PWM-Eingang + GPIO-Pin + Einkanal
   V3  kein GPIO-Eingang          – nur BUS-Kanal + Einkanal (SBUS/CRSF)
                                     + Port B/C (siehe unten)
   Ein alter NVS-Stand mit den frueher existierenden Werten V4/V5 wird beim
   Booten auf V3 begrenzt, Port B/C starten dabei auf "Aus" (siehe
   config.cpp/loadConfig()).

   QUELLEN (V1/V2): BUS Kanal Low/High (1-16), PWM-Eingang Low/High (1-6),
                     GPIO-Pin direkt (1-6), Einkanal (1-8), MKan (1-16),
                     Dauerbetrieb, Deaktiviert
   QUELLEN (V3):     BUS Kanal Low/High (1-16), Einkanal (1-8), MKan (1-16),
                     Dauerbetrieb, Deaktiviert
   MOTOR SPEED:      V1/V2: BUS Kanal + PWM Pin  |  V3: BUS Kanal

   ── Pin-Belegung (alle Versionen) ──────────────────────────────────────────
   GPIO 13: WiFi-Aktivierung (LOW beim Booten = AP-Modus)
   GPIO 16: CRSF RX / SBUS RX  |  GPIO 17: CRSF TX
   GPIO 05: SD_CS  |  GPIO 18: SD_CLK  |  GPIO 19: SD_MISO  |  GPIO 23: SD_MOSI
   GPIO 21: I2S_DOUT  |  GPIO 25: I2S_LRC  |  GPIO 26: I2S_BCLK
   Port B (nur V3, zusätzlich): GPIO 32 RX  |  GPIO 33 TX
     Schaltung (S.Port-Rolle): GPIO33 -[1N4148 Anode->Kathode]-> S.Port Signal <- GPIO32

   ── Das Port-Modell (V3) ──────────────────────────────────────────────────
   Port A = RC-Eingang, fest (CRSF oder SBUS-Familie).
   Port B = echter zweiter Hardware-UART (GPIO32/33), Rolle waehlbar in Web
            UND CRSF/Lua-Menue, mit frei einstellbarer Baudrate je Rolle:
            Aus, S.Port-Master (LiPo-Telemetrie an FrSky-Empfaenger),
            CRSF (zweite, unabhaengige CRSF-Instanz), Hott (Menue
            vorhanden, Treiber noch offen) oder generischer Raw-UART.
   Port C = GPS (SoftwareSerial GPIO27/14) fuer CRSF-GPS-Telemetrie
            (Geschwindigkeit/Position an den Sender) - unabhaengiger
            An/Aus-Schalter. Kollidiert mit SBUS als Bus-Protokoll (beide
            belegen GPIO27), siehe setup()/gpsPinConflict.
   Welches Modul (port_function.cpp) fuer welche Port-B-Rolle zustaendig
   ist, kommt aus einer zentralen Registry (port_function.h/.cpp), damit
   Web-UI, Lua-Menue und setup()/loop() dieselbe Optionsliste/Baudraten-
   Defaults verwenden statt sie an mehreren Stellen von Hand synchron zu
   halten. Ein rein hardwareunabhaengiger Spiegel dieser Logik liegt
   zusaetzlich unter lib/port_registry/ und wird per PlatformIO-"native"-
   Environment (siehe platformio.ini, test/test_native) ohne ESP32-Board
   getestet - muss bei Aenderungen von Hand synchron gehalten werden
   (siehe Kommentar dort).

   ── WLAN & Weboberflaeche ─────────────────────────────────────────────────
   Der Access Point (SoftAP) laesst sich auf drei Wegen aktivieren:
     - GPIO13 kurz auf GND beim Einschalten (immer verfuegbar, auch ganz
       ohne Sender).
     - Manueller Schalter in Web-Tab "WiFi" bzw. CRSF/Lua-Feld 174 ("WLAN
       jetzt") - wirkt sofort, ohne Neustart, ohne Persistierung.
     - Automatischer Failsafe (Web-Tab "WiFi"/Lua-Felder 175-176): schaltet
       das WLAN von selbst ein, wenn laenger als die eingestellte Zeit
       (5-240s, Standard 60s) kein gueltiges RC-Signal anliegt - schaltet
       NUR ein, nie automatisch wieder aus (verhindert, dass eine laufende
       WLAN-Konfiguration/ein laufendes OTA-Update abbricht, sobald kurz
       wieder ein gueltiges Signal hereinkommt).
   Ueber die Weboberflaeche lassen sich Sounds/Konfiguration verwalten,
   Sound-Dateien und Konfigurations-Backups hoch-/herunterladen bzw.
   loeschen, und eine neue Firmware direkt per WLAN aufspielen (OTA, siehe
   WebServerManager.cpp) - ohne das Modul auszubauen. Empfehlung aus der
   Praxis: ein OTA-Update moeglichst bei ueber ESC/Akku bestromtem Modul
   durchfuehren, nicht nur ueber USB (manche USB-Anschluesse liefern beim
   Flash-Schreiben nicht genug Strom, ein dadurch ausgeloester Brownout-
   Reset bricht den Upload ab). WiFi.softAP() gilt projektweit als die
   fehleranfaelligste Einzeloperation: ein Start mit noch stark belegtem/
   fragmentiertem Heap (z.B. nachdem Sounds/Uploads liefen) kann
   fehlschlagen - enableAP() (WebServerManager.cpp) loggt deshalb bei
   jedem Start den freien Heap und meldet einen Fehlschlag explizit.

   ── CRSF-/Lua-Parametermenue ───────────────────────────────────────────
   Bei RC-System ELRS (CRSF) ist das Modul zusaetzlich vollstaendig ueber
   das native CRSF-Parametermenue des Senders konfigurierbar (z.B. als
   Lua-Script in EdgeTX). CRSF_PARAM_COUNT Feld-IDs, lueckenlos 1..181 -
   die vollstaendige Feldliste steht direkt beim Parameter-System weiter
   unten. Jedes Modul erhaelt seine CRSF-Busadresse (0xC0+WM-Adresse) und
   seinen Telemetrie-Zeitslot aus der WM-Adresse (Wilhelm-Meier-Schema),
   damit sich mehrere Module am selben Bus nicht gegenseitig stoeren.

   ── Weitere Module dieses Projekts ───────────────────────────────────────
   config.cpp/.h         Persistente Konfiguration (NVS/Preferences),
                          Migration alter Datenformate.
   WebServerManager.cpp   Weboberflaeche, REST-API, OTA-Update.
   XT_I2S_Audio.cpp/.h    WAV-Wiedergabe ueber I2S, SD-Kartenzugriff.
   crsf_esp32.cpp/.h      CRSF-Protokoll (Frame-Parsing, Telemetrie,
                          Geraete-Discovery/Parameter-Handshake).
   sport_lipo.cpp/.h      S.Port-Telemetrie (Port B, LiPo-Zellspannungen).
   gps_speed.cpp/.h       GPS-Auswertung (Port C) fuer CRSF-GPS-Telemetrie.
   port_function.cpp/.h   Port-B-Rollen-Registry (siehe oben).

   Rahmenbedingung dieses Projekts: Aenderungen werden ohne Zugriff auf
   einen Compiler oder ein echtes Geraet vorgenommen - die Probe am
   Geraet obliegt dem Nutzer.
*/

#include <Arduino.h>
#include "XT_I2S_Audio.h"
#include <WiFi.h>
#include "sbus.h"
#include "crsf_esp32.h"
#include "config.h"
#include "WebServerManager.h"
#include "sport_lipo.h"
#include "gps_speed.h"   // GPS-Geschwindigkeit (Port C)
#include "port_function.h"   // Port-B-Rollen-Registry

uint16_t Version = 717;  // Firmware-Version fuer Web-/Lua-Anzeige: major*100+minor (717 -> "7.17")
char versionString[6];
bool gpsPinConflict = false; // true, wenn Port C (GPS) UND SBUS gleichzeitig gewaehlt sind (GPIO27-Konflikt, siehe setup())
// Ob der AP gerade aktiv ist, wird NICHT hier gespiegelt: loop() fragt dafuer
// direkt WebServerManager::isApActive() ab (die eigentliche Variable lebt in
// WebServerManager.cpp) - WLAN kann jederzeit zur Laufzeit ein-/ausgeschaltet
// werden (manueller Schalter oder Auto-Failsafe, siehe wifiFailsafeCheck()),
// eine separate lokale Kopie des Zustands koennte sonst aus dem Takt geraten
// und server.handleClient() wuerde trotz aktivem AP nicht mehr aufgerufen.

// ── Zustand ───────────────────────────────────────────────────────────
bool Sound_on[RCSOUND_NUM_SLOTS]     = {};
bool Sound_play[RCSOUND_NUM_SLOTS]   = {};
bool Sound_on_web[RCSOUND_NUM_SLOTS] = {};

bool Sound_on_Motor       = false;
bool Sound_on_Motor_state = false;
bool engine_break         = false;
unsigned long shutdown_timer = 0;

enum Engine_State : uint8_t { OFF, STARTING, RUNNING, STOPPING };
volatile uint8_t engine_State = OFF;

// Boot-Snapshot: RC-System, Port-B-Rolle/Baudrate und Port-C(GPS)-Zustand
// werden einmalig in setup() aus config uebernommen. Ein spaeteres Aendern
// ueber Web/Lua (Felder 161/171-173) schreibt sofort in config, wirkt aber
// bewusst erst nach einem Neustart - die zugehoerigen UARTs sind schon
// initialisiert und duerfen waehrend des Betriebs nicht umgeschaltet werden.
int      Einkanal_RC_System_boot = 0;
int      PortB_Mode_boot        = PORTB_OFF;
uint32_t PortB_Baud_boot        = PORTB_BAUD_DEFAULT_CRSF;
int      PortC_GPS_Enabled_boot = 0;
// Liefert den fuer Port B freien Hardware-UART - Port A belegt bereits den
// jeweils anderen (CRSF=Serial2, SBUS-Familie=Serial1). Dieselbe Logik wird
// auch in sport_lipo.cpp verwendet.
static HardwareSerial* portBFreeSerial() {
    return (config.Einkanal_RC_System == 4) ? &Serial1 : &Serial2;
}
uint16_t einkanal_Data           = 0;   // 1 Bit/Schalter - Quelltyp "Einkanal" 70-77
uint16_t einkanal_SpeicherWM     = 0;
// Persistente Bit-Register je SBUS-Gruppe (analog zu einkanal_SpeicherWM,
// aber pro Gruppe getrennt) - siehe einkanalFunctionSBUSGroup().
uint16_t einkanal_SpeicherWM_Grp[RCSOUND_NUM_GROUPS] = {};

// Je ein 8-Bit-Wert (1 Bit/Schalter) aus den 2 unabhaengig konfigurierbaren
// Gruppen sowie deren Kombination zu einem 16-Bit-Flachwert - Quelltyp
// "MKan" 80-95 (siehe Config()). Je nach RC-System wird eine Gruppe entweder
// per CRSF-Gruppenadresse (config.EK_Gruppen_Adresse[], siehe
// updateGroupFromAddress()) oder per fest zugewiesenem SBUS-Kanal
// (config.SBUS_Gruppen_Channel[], siehe einkanalFunctionSBUSGroup())
// befuellt - beide Wege sind gegenseitig exklusiv, da loop() immer nur
// einen der beiden Busse gleichzeitig bedient.
uint8_t  einkanal_Gruppe[RCSOUND_NUM_GROUPS] = {};
uint32_t einkanal_Flat16          = 0;

int      Source_Ebenen_Um_Kanal_wert = 0;
int      Source_Ebenen_Kanal_wert    = 0;


// ── Hardware ──────────────────────────────────────────────────────────
#define SD_CS    5
#define I2S_DOUT 21
#define I2S_BCLK 26
#define I2S_LRC  25
const uint8_t WifiPin = 13;

// GPIO-Pins: Index 0-1 = CRSF RX/TX (immer), 2-5 = Eingangs-Pins
// V1: {16,17, 22, 0, 2, 4}
// V2: {16,17, 14,27,32,33}
// V3: nicht verwendet
uint8_t Input_Pin[6] = {16, 17, 22, 0, 2, 4};  // Default V1

// ── Throttle / Rampe ─────────────────────────────────────────────────
unsigned long previousTime_ramp = 0;
float last_throttle = 0.0f;
float throttle      = 0.0f;

char ssid[32];
char password[64];
unsigned long currentTime = 0;

// ── SBUS ──────────────────────────────────────────────────────────────
bfs::SbusRx   sbus_rx(&Serial1, 16, 27, true);
bfs::SbusData sbus_data;
bool BUS_OK = false;

// ── CRSF ──────────────────────────────────────────────────────────────
CRSF crsf;       // Port A - der Haupt-RC-Bus
// Zweite, unabhaengige CRSF-Instanz fuer Port B (Rolle PORTB_CRSF) - eigener
// UART, eigene Telemetrie-/Kanal-Puffer (serial_data/linkStats/info sind
// Member der CRSF-Klasse, siehe crsf_esp32.h, damit sich zwei Instanzen
// nicht gegenseitig beeinflussen).
CRSF crsfPortB;  // Port B, nur aktiv wenn PortB_Mode_boot == PORTB_CRSF
#define MWset       0x01
#define MWprop      0x02
#define MWset4      0x07
#define MWset4m     0x09
#define Multiswitch 0xA1

static constexpr unsigned long CRSF_TIMEOUT_MS = 2000;
static unsigned long lastCrsfPacket = 0;

static void checkCrsfTimeout() {
    if (Einkanal_RC_System_boot != 4) return;
    if (millis() - lastCrsfPacket > CRSF_TIMEOUT_MS) BUS_OK = false;
}

uint16_t channel_output[16] = {};

// ── I2S Audio ─────────────────────────────────────────────────────────
XT_I2S_Class I2SAudio(I2S_LRC, I2S_BCLK, I2S_DOUT, I2S_NUM_0);

XT_Wav_Class Sound_loop ("/loop.wav");
XT_Wav_Class Sound_shut ("/shut.wav");
XT_Wav_Class Sound_start("/start.wav");
XT_Wav_Class Sound1("/sound1.wav");   XT_Wav_Class Sound2("/sound2.wav");
XT_Wav_Class Sound3("/sound3.wav");   XT_Wav_Class Sound4("/sound4.wav");
XT_Wav_Class Sound5("/sound5.wav");   XT_Wav_Class Sound6("/sound6.wav");
XT_Wav_Class Sound7("/sound7.wav");   XT_Wav_Class Sound8("/sound8.wav");
// v5: Slots 9-24 neu (Ebenen-Erweiterung) - gleiches Namensschema, /soundNN.wav auf SD
XT_Wav_Class Sound9 ("/sound9.wav");  XT_Wav_Class Sound10("/sound10.wav");
XT_Wav_Class Sound11("/sound11.wav"); XT_Wav_Class Sound12("/sound12.wav");
XT_Wav_Class Sound13("/sound13.wav"); XT_Wav_Class Sound14("/sound14.wav");
XT_Wav_Class Sound15("/sound15.wav"); XT_Wav_Class Sound16("/sound16.wav");
XT_Wav_Class Sound17("/sound17.wav"); XT_Wav_Class Sound18("/sound18.wav");
XT_Wav_Class Sound19("/sound19.wav"); XT_Wav_Class Sound20("/sound20.wav");
XT_Wav_Class Sound21("/sound21.wav"); XT_Wav_Class Sound22("/sound22.wav");
XT_Wav_Class Sound23("/sound23.wav"); XT_Wav_Class Sound24("/sound24.wav");

XT_Wav_Class* Sounds[RCSOUND_NUM_SLOTS] = {
    nullptr,
    &Sound1,&Sound2,&Sound3,&Sound4,&Sound5,&Sound6,&Sound7,&Sound8,
    &Sound9,&Sound10,&Sound11,&Sound12,&Sound13,&Sound14,&Sound15,&Sound16,
    &Sound17,&Sound18,&Sound19,&Sound20,&Sound21,&Sound22,&Sound23,&Sound24
};

// ── PWM-Eingang (V1/V2) ───────────────────────────────────────────────
volatile unsigned int PWM_pulse_width[6] = {3000,3000,3000,3000,3000,3000};
volatile unsigned int PWM_prev_time[6]   = {0,0,0,0,0,0};
// Zeitstempel des letzten gueltigen Pulses je Kanal - ohne das wuerden
// abgerissene/eingefrorene PWM-Signale ihren letzten Wert behalten und
// faelschlich weiter als gueltiger Zustand durchgehen (siehe pwmValid()).
volatile unsigned long PWM_lastPulseMs[6] = {0,0,0,0,0,0};
static constexpr unsigned long PWM_TIMEOUT_MS = 300;

static inline void IRAM_ATTR handlePWM(uint8_t idx) {
    if (digitalRead(Input_Pin[idx])) PWM_prev_time[idx] = micros();
    else { PWM_pulse_width[idx] = micros() - PWM_prev_time[idx]; PWM_lastPulseMs[idx] = millis(); }
}
void IRAM_ATTR ISR_PWM_0() { handlePWM(0); }
void IRAM_ATTR ISR_PWM_1() { handlePWM(1); }
void IRAM_ATTR ISR_PWM_2() { handlePWM(2); }
void IRAM_ATTR ISR_PWM_3() { handlePWM(3); }
void IRAM_ATTR ISR_PWM_4() { handlePWM(4); }
void IRAM_ATTR ISR_PWM_5() { handlePWM(5); }
void (*ISR_PWM[6])() = {ISR_PWM_0,ISR_PWM_1,ISR_PWM_2,ISR_PWM_3,ISR_PWM_4,ISR_PWM_5};

static bool pwm_isr_attached[6] = {};
void attachPWM_ISR(uint8_t pinIdx) {
    // Nur in V1/V2 aktiv
    if (config.Hardware_Config >= 2) return;
    if (pinIdx < 6 && !pwm_isr_attached[pinIdx]) {
        attachInterrupt(digitalPinToInterrupt(Input_Pin[pinIdx]), ISR_PWM[pinIdx], CHANGE);
        pwm_isr_attached[pinIdx] = true;
    }
}

// ── Vorwärtsdeklarationen ─────────────────────────────────────────────
void Config(); void SDCardInit(); void LoadFiles();
void AllocateSoundBuffers(); // reserviert die Sample-Puffer erst in setup() NACH dem WLAN-AP-Start, siehe dort und XT_I2S_Audio.h
static inline bool pwmValid(uint8_t idx);
void einkanalFunction(uint16_t ch);
void einkanalFunctionSBUSGroup(uint8_t g, uint16_t ch); // SBUS-Pendant zu updateGroupFromAddress() fuer "MKan" (Quelltyp 80-95)
void einkanalFunctionCRSF(); uint8_t compressSwitches(uint16_t state);
static void updateGroupFromAddress(uint8_t addr, uint8_t bits); // Gruppenadressen-Abgleich fuer "MKan" (Quelltyp 80-95)
float ramp_throttle(float t); float floatMap(float x,float a,float b,float c,float d);
void handleSound(uint8_t idx, XT_Wav_Class* snd);
static void crsfSendParam(uint8_t idx);
static void crsfWriteParam(uint8_t idx, uint8_t val);

// ======== CRSF-Parametersystem (176 Parameter) ==========================
// lib/sound_slots/sound_slots.h und test/test_sound_slots/ sind nicht Teil
// des esp32dev-Builds (nichts in src/ bindet sie ein) und bleiben ungenutzt
// liegen.
//
//  0: Root  1: Version Info
//  2: Folder Motor {3..15}
//   3: Sel MotorMode  4: Sel EINModus  5: Sel QuelleEIN  6: U8 KanalEIN
//   7: Sel QuelleSpd  8: U8 KanalSpd   9: U8 Vol
//  10: U8 Drzmin  11: U8 Drzmax/2  12: U8 Standgas  13: U8 Rampe  14: U8 Totband
//  15: Info HW-Version (aktuell aktive Hardware)
//
//  Sound s(1-24): fi=16+(s-1)*6  → endet bei 159
//   fi+0:Folder fi+1:Sel Quelle fi+2:U8 Kanal fi+3:U8 Vol fi+4:Sel Mode fi+5:Sel Test
//
//  160: Folder Einstellungen {161..169, 171..181}
//   161:Sel RC-Sys  162:U8 ModAdr  163:U8 EKKanal  164:Sel EKMode
//   165:Sel HardwareConfig  V1;V2;V3  (siehe config.h)
//   166:U8 PWM min(/16)  167:U8 PWM max(/16)
//   168:U8 Gruppenadr.1  169:U8 Gruppenadr.2  (255=aus, nur CRSF)
//  170: Info Gerätename
//   171:Sel Port B Modus  Aus;CRSF;S.Port;Hott (bald);Raw
//   172:Sel Port B Baudrate  aus PORTB_BAUD_LIST_STR (kuratierte Liste, siehe dort)
//   173:Sel Port C (GPS)  Aus;An
//   174:Sel WLAN jetzt  Aus;Ein  (wirkt SOFORT, nicht persistiert - siehe crsfWriteParam)
//   175:Sel Auto-WLAN bei Signalverlust  Aus;Ein  (persistiert, siehe config.WifiAutoEnable)
//   176:U8 Auto-WLAN Timeout (s)  (5-240, siehe config.WifiAutoTimeoutSec)
//   177:U8 SBUS Gruppe1 Kanal (255=aus)  178:Sel SBUS Gruppe1 Adresse  Adr0-3
//   179:U8 SBUS Gruppe2 Kanal (255=aus)  180:Sel SBUS Gruppe2 Adresse  Adr0-3
//   (177-180: SBUS-Pendant zu 168/169, siehe EK_Gruppen_Adresse/
//   SBUS_Gruppen_Channel in config.h)
//   181:Sel WLAN dauerhaft an  Aus;Ein  (persistiert, siehe config.WifiAlwaysOn;
//   Default AN - ueberstimmt 174/175/176 komplett, siehe wifiFailsafeCheck())

static constexpr uint8_t CRSF_PARAM_COUNT = 181;   // Field-IDs lueckenlos 1..181 (170 Root-Info, 171-181 in Folder 160)

// ── CRSF-Geraeteadresse + Ping-Slot aus der WM-Adresse (Wilhelm-Meier-Schema) ──
// Jedes Modul bekommt eine eindeutige CRSF-Bus-Adresse aus 0xC0..0xCF.
// Adresse   = 0xC0 + WM-Adresse         (WM 0 -> 0xC0, WM 2 -> 0xC2, ...)
// Slot-Nr   = (Adresse - 0xC0) * 2 = WM-Adresse * 2   (Wilhelm Meiers Formel)
// Die Ping-Antwort/Telemetrie wird erst im eigenen Zeit-Slot gesendet, damit
// sich die Antworten mehrerer Module am selben Empfaenger nicht ueberlappen.
#define CRSF_SLOT_MS 5   // Dauer pro Slot-Einheit (ms): Soundmodul (WM2=Slot4) antwortet 20ms versetzt zum Multiswitch (WM0=Slot0)
static inline uint8_t  crsfAddrFromWM()  { return 0xC0 + (uint8_t)constrain(config.modul_adress, 0, 15); }
static inline uint16_t crsfSlotDelayMs() { return (uint16_t)((uint8_t)constrain(config.modul_adress, 0, 15) * 2) * CRSF_SLOT_MS; }

// ── Typ-Mapping Quelle Motor/Sound EIN ───────────────────────────────
// Typ: 0=BUS-L(0-15) 1=BUS-H(20-35) 2=PWM-L(40-45) 3=PWM-H(50-55)
//      4=Pin(60-65)  5=Einkanal(70-77) 6=Mehrfachkanal/MKan(80-95,
//      siehe RCSOUND_NUM_GROUPS/EK_Gruppen_Adresse - 2 Gruppen x 8 Kanaele)
//      7=Dauerbetrieb(200) 8=Deaktiviert(999)
// V3 ignoriert Typen 2/3/4 in Config()

static uint8_t einTyp(int c) {
    if(c==999)return 8; if(c==200)return 7;
    if(c>=80&&c<=95)return 6; if(c>=70&&c<=77)return 5;
    if(c>=60&&c<=65)return 4; if(c>=50&&c<=55)return 3;
    if(c>=40&&c<=45)return 2; if(c>=20&&c<=35)return 1;
    if(c>=0&&c<=15)return 0; return 8;
}

// Prueft, ob ein "Quelle"-Rohwert einem der von einTyp() erkannten,
// gueltigen Bereiche entspricht. Bewusst NICHT static: config.cpp (Boot-
// Migration in loadConfig()) und WebServerManager.cpp (Eingabepruefung in
// handleApiSound(), greift auch bei jedem Konfigurations-Import) nutzen
// dieselbe Definition wie einTyp() - keine zweite, potenziell abweichende
// Bereichspruefung. Ein ungueltiger Rohwert (z.B. aus einer alten
// Konfigurationsdatei mit einem inzwischen entfallenen Wertebereich) wird
// beim Laden/Import auf 999 (Deaktiviert) zurueckgesetzt, statt unveraendert
// in config.Source_Start_Sound[] stehen zu bleiben.
bool isValidEinSource(int c) {
    return c==999 || c==200 ||
           (c>=80&&c<=95) || (c>=70&&c<=77) || (c>=60&&c<=65) ||
           (c>=50&&c<=55) || (c>=40&&c<=45) || (c>=20&&c<=35) ||
           (c>=0&&c<=15);
}
static uint8_t einNr(int c) {
    if(c>=80&&c<=95)return c-80+1; if(c>=70&&c<=77)return c-70+1;
    if(c>=60&&c<=65)return c-60+1; if(c>=50&&c<=55)return c-50+1;
    if(c>=40&&c<=45)return c-40+1; if(c>=20&&c<=35)return c-20+1;
    if(c>=0&&c<=15)return c+1; return 1;
}
static int setEin(uint8_t t, uint8_t nr) {
    uint8_t i=(nr>0)?nr-1:0;
    if(t==0)return min(i,(uint8_t)15);
    if(t==1)return 20+min(i,(uint8_t)15);
    if(t==2)return 40+min(i,(uint8_t)5);
    if(t==3)return 50+min(i,(uint8_t)5);
    if(t==4)return 60+min(i,(uint8_t)5);
    if(t==5)return 70+min(i,(uint8_t)7);
    if(t==6)return 80+min(i,(uint8_t)15);
    if(t==7)return 200; return 999;
}
static uint8_t einMaxNr(uint8_t t) {
    if(t==0||t==1)return 16; if(t==2||t==3||t==4)return 6;
    if(t==5)return 8; if(t==6)return 16; return 1;
}

// ── Typ-Mapping Quelle Speed ──────────────────────────────────────────
// Typ: 0=BUS Kanal(0-15) 1=PWM Pin(20-25) 2=Deaktiviert(999)
// V3 ignoriert Typ 1 in Config()
static uint8_t spdTyp(int c) {
    if(c>=0&&c<=15)return 0; if(c>=20&&c<=25)return 1; return 2;
}
static uint8_t spdNr(int c) {
    if(c>=0&&c<=15)return c+1; if(c>=20&&c<=25)return c-20+1; return 1;
}
static int setSpd(uint8_t t, uint8_t nr) {
    uint8_t i=(nr>0)?nr-1:0;
    if(t==0)return min(i,(uint8_t)15);
    if(t==1)return 20+min(i,(uint8_t)5);
    return 999;
}

static bool testSoundActive[RCSOUND_NUM_SLOTS]={};

// ── Hilfsfunktion: Aktuelle HW-Version als String ─────────────────────
static const char* hwVersionStr() {
    switch(config.Hardware_Config) {
        case 0: return "V1 (GPIO 22,0,2,4)";
        case 1: return "V2 (GPIO 14,27,32,33)";
        default: return "V3 (Port A/B/C)"; // S.Port/GPS sind Port-B/C-Rollen, keine eigene Hardware-Stufe
    }
}

// ── Port B: Baudraten-Liste fuer das CRSF/Lua-Menue ───────────────────
// CRSF-Parameter dieses Typs (TEXT_SELECTION) uebertragen ihren Wert als
// 1 Byte (siehe paramWriteValue in crsf_esp32.cpp) - eine frei eintippbare
// Baudrate bis 420000 passt da nicht rein. Im Lua-Menue daher eine
// kuratierte Auswahl gaengiger Baudraten; im Web-UI (JSON/HTTP, keine 1-Byte-
// Grenze) bleibt die Baudrate wie gewuenscht frei eingebbar.
static const char* PORTB_BAUD_LIST_STR = "9600;19200;38400;57600;115200;230400;420000";
static const uint32_t portBBaudList[] = {9600,19200,38400,57600,115200,230400,420000};
static constexpr uint8_t PORTB_BAUD_LIST_COUNT = 7;
static uint8_t portBBaudToIndex(uint32_t baud) {
    for (uint8_t i=0;i<PORTB_BAUD_LIST_COUNT;i++) if (portBBaudList[i]==baud) return i;
    // Unbekannter/frei ueber's Web eingegebener Wert - im Lua-Menue den
    // naechstgelegenen Listeneintrag ANZEIGEN, ohne den gespeicherten Wert
    // zu veraendern (das passiert nur, wenn im Lua-Menue selbst geschrieben wird).
    uint8_t best=0; uint32_t bestDiff=0xFFFFFFFF;
    for (uint8_t i=0;i<PORTB_BAUD_LIST_COUNT;i++) {
        uint32_t diff=(baud>portBBaudList[i])?(baud-portBBaudList[i]):(portBBaudList[i]-baud);
        if (diff<bestDiff){bestDiff=diff;best=i;}
    }
    return best;
}

// ── CRSF Quelle-Speed Optionen je nach HW ─────────────────────────────
static const char* spdSelStr() {
    return (config.Hardware_Config < 2)
        ? "BUS Kanal;PWM Pin;Deaktiviert"
        : "BUS Kanal;Deaktiviert";
}
static uint8_t spdSelMax() {
    return (config.Hardware_Config < 2) ? 2 : 1;
}
static uint8_t spdSelVal() {
    uint8_t t = spdTyp(config.Source_Speed_Sound_0);
    // V3: PWM (t==1) → zeige als Deaktiviert
    if (config.Hardware_Config >= 2 && t == 1) return 1;
    return t;
}

// ── CRSF Quelle EIN Optionen: V3 zeigt nur L/H/EK/MKan/Dauer/Aus ────────
// "MKan" (Mehrfachkanal, Typ 6) ist fuer alle Hardware-Varianten waehlbar,
// auch V3 (kein GPIO) - siehe Config() fuer die Auswertung. Genau die
// Hardware, mit der die Multiswitch-Box/das WM-Widget normalerweise
// betrieben wird.
static const char* einSelStr() {
    return (config.Hardware_Config < 2)
        ? "L;H;PWM-L;PWM-H;Pin;EK;MKan;Dauer;Aus"
        : "L;H;EK;MKan;Dauer;Aus";
}
static uint8_t einSelMax() { return (config.Hardware_Config < 2) ? 8 : 5; }
static uint8_t einSelVal(int cfg) {
    uint8_t t = einTyp(cfg);
    if (config.Hardware_Config >= 2) {
        // V3+: PWM/Pin → Deaktiviert (5, kein GPIO auf dieser Hardware)
        if (t == 2 || t == 3 || t == 4) return 5;
        // V3+ Mapping: 0=L,1=H,5→2=EK,6→3=Eben,7→4=Dauer,8→5=Aus
        if (t==5) return 2; if (t==6) return 3; if (t==7) return 4; if (t>=8) return 5;
        return t; // 0,1
    }
    return t; // V1/V2: vollständig 0-8
}
static int einSelWrite(uint8_t val, int oldCfg) {
    uint8_t nr = einNr(oldCfg);
    if (config.Hardware_Config >= 2) {
        // V3+ Mapping Auswahl→Typ: 0=L,1=H,2=EK,3=Eben,4=Dauer,5=Aus
        static const uint8_t v3map[] = {0,1,5,6,7,8};
        uint8_t t = (val < 6) ? v3map[val] : 8;
        return setEin(t, nr);
    }
    return setEin(val, nr); // V1/V2: direkt
}

// ======== CRSF: Parameter senden =====================================
static void crsfSendParam(uint8_t idx) {
    char buf[48];

    if(idx==0) {
        crsf.send_param_response_CRSF_FOLDER(0,0,"",
            {1,2,
             16,22,28,34,40,46,52,58,64,70,76,82,88,94,100,106,112,118,124,130,136,142,148,154,
             160,170});
    }
    else if(idx==1) {
        // versionString wird in setup() aus der Version-Konstante berechnet
        // (z.B. "7.13") - dieselbe Quelle, die auch die Weboberflaeche fuer
        // ihre Versionsanzeige benutzt.
        snprintf(buf,sizeof(buf),"%s %s",versionString,hwVersionStr());
        crsf.send_param_response_CRSF_INFO(1,0,"Version",buf);
    }

    // ── Motor (2..15) ─────────────────────────────────────────────────
    else if(idx==2) crsf.send_param_response_CRSF_FOLDER(2,0,"Motor",
        {3,4,5,6,7,8,9,10,11,12,13,14,15});
    else if(idx==3) crsf.send_param_response_CRSF_TEXT_SELECTION(3,2,
        "Motor Mode","Eine Richtung;Zwei Richtungen",
        (uint8_t)constrain(config.throttle_mode,0,1),0,1);
    else if(idx==4) crsf.send_param_response_CRSF_TEXT_SELECTION(4,2,
        "Motor EIN Modus","Normal;Toggle",
        (uint8_t)constrain(config.engine_on_toggle,0,1),0,1);
    else if(idx==5) crsf.send_param_response_CRSF_TEXT_SELECTION(5,2,
        "Quelle Motor", einSelStr(),
        einSelVal(config.Source_Start_Sound[0]),0,einSelMax());
    else if(idx==6) {
        uint8_t t=einTyp(config.Source_Start_Sound[0]);
        bool noPin=(config.Hardware_Config>=2&&(t==2||t==3||t==4));
        if(t==7||t==8||noPin)
            crsf.send_param_response_CRSF_INFO(6,2,"Kanal EIN",
                (t==7||noPin)?"Dauerbetrieb/Deakt.":"Deaktiviert");
        else crsf.send_param_response_CRSF_UINT8(6,2,"Kanal Nr EIN",
            einNr(config.Source_Start_Sound[0]),1,einMaxNr(t),"");
    }
    else if(idx==7) crsf.send_param_response_CRSF_TEXT_SELECTION(7,2,
        "Quelle Speed", spdSelStr(), spdSelVal(),0,spdSelMax());
    else if(idx==8) {
        uint8_t t=spdTyp(config.Source_Speed_Sound_0);
        bool deakt=(t==2)||(config.Hardware_Config>=2&&t==1);
        if(deakt) crsf.send_param_response_CRSF_INFO(8,2,"Kanal Speed","Deaktiviert");
        else crsf.send_param_response_CRSF_UINT8(8,2,"Kanal Nr Speed",
            spdNr(config.Source_Speed_Sound_0),1,(t==1)?6:16,"");
    }
    else if(idx==9)  crsf.send_param_response_CRSF_UINT8(9, 2,"Volumen Motor",
        (uint8_t)constrain(config.Volumen_Sound[0],0,200),0,200,"");
    else if(idx==10) crsf.send_param_response_CRSF_UINT8(10,2,"Drehzahl min %",
        (uint8_t)constrain(config.Min_Speed_Sound_0,0,200),0,200,"%");
    else if(idx==11) crsf.send_param_response_CRSF_UINT8(11,2,"Drehzahl max /2",
        (uint8_t)constrain(config.Max_Speed_Sound_0/2,50,255),50,255,"%");
    else if(idx==12) crsf.send_param_response_CRSF_UINT8(12,2,"Motor aus Standgas",
        (uint8_t)constrain(config.shutdowndelay,0,60),0,60,"s");
    else if(idx==13) crsf.send_param_response_CRSF_UINT8(13,2,"Motor Rampe %/s",
        (uint8_t)constrain(config.throttle_ramp,0,50),0,50,"");
    else if(idx==14) crsf.send_param_response_CRSF_UINT8(14,2,"Standgas Totband",
        (uint8_t)constrain(config.throttle_dead_band,0,50),0,50,"%");
    else if(idx==15) {
        if (hardwareConfigRestartPending()) {
            char b2[40]; snprintf(b2,sizeof(b2),"%s (Neustart noetig!)",hwVersionStr());
            crsf.send_param_response_CRSF_INFO(15,2,"Hardware (aktiv)",b2);
        } else {
            crsf.send_param_response_CRSF_INFO(15,2,"Hardware",hwVersionStr());
        }
    }

    // ── Sound 1-24: fi=16+(s-1)*6, endet bei 159 ──────────────────────────
    else if(idx>=16&&idx<=159) {
        uint8_t s=(idx-16)/6+1, sub=(idx-16)%6, fi=16+(s-1)*6;
        switch(sub) {
        case 0:
            snprintf(buf,sizeof(buf),"Sound %d",s);
            crsf.send_param_response_CRSF_FOLDER(fi,0,buf,
                {(uint8_t)(fi+1),(uint8_t)(fi+2),
                 (uint8_t)(fi+3),(uint8_t)(fi+4),(uint8_t)(fi+5)});
            break;
        case 1:
            crsf.send_param_response_CRSF_TEXT_SELECTION(fi+1,fi,
                "Quelle",einSelStr(),
                einSelVal(config.Source_Start_Sound[s]),0,einSelMax());
            break;
        case 2: {
            uint8_t t=einTyp(config.Source_Start_Sound[s]);
            bool noPin=(config.Hardware_Config>=2&&(t==2||t==3||t==4));
            if(t==7||t==8||noPin)
                crsf.send_param_response_CRSF_INFO(fi+2,fi,"Kanal Nr",
                    t==7?"Dauerbetrieb":"Deaktiviert");
            else crsf.send_param_response_CRSF_UINT8(fi+2,fi,"Kanal Nr",
                einNr(config.Source_Start_Sound[s]),1,einMaxNr(t),"");
            break;
        }
        case 3: crsf.send_param_response_CRSF_UINT8(fi+3,fi,"Volumen",
            (uint8_t)constrain(config.Volumen_Sound[s],0,200),0,200,""); break;
        case 4: crsf.send_param_response_CRSF_TEXT_SELECTION(fi+4,fi,
            "Wiedergabe Mode","Normal;Loop;Tippbetrieb",
            (uint8_t)constrain(config.Mode_Sound[s],0,2),0,2); break;
        case 5: crsf.send_param_response_CRSF_TEXT_SELECTION(fi+5,fi,
            "Test Sound","Aus;Ein",testSoundActive[s]?1:0,0,1); break;
        }
    }

    // ── Einstellungen (160..169) ──────────────────────────────────────────
    else if(idx==160) crsf.send_param_response_CRSF_FOLDER(160,0,"Einstellungen",
        {161,162,163,164,165,166,167,168,169,171,172,173,174,175,176,177,178,179,180,181});
    else if(idx==161) crsf.send_param_response_CRSF_TEXT_SELECTION(161,160,
        "RC-System","FrSky;FlySky;ELRS SBUS;Hott;ELRS CRSF",
        (uint8_t)constrain(config.Einkanal_RC_System,0,4),0,4);
    else if(idx==162) crsf.send_param_response_CRSF_UINT8(162,160,"Modul Adresse",
        (uint8_t)constrain(config.modul_adress,0,20),0,20,"");
    else if(idx==163) {
        uint8_t v=(config.Einkanal_Channel==999)?255:(uint8_t)constrain(config.Einkanal_Channel,0,15);
        crsf.send_param_response_CRSF_UINT8(163,160,"Einkanal Kanal (255=aus)",v,0,255,"");
    }
    else if(idx==164) {
        uint8_t cur=0;
        if(config.Einkanal_mode==0)cur=0;
        else if(config.Einkanal_mode>=10&&config.Einkanal_mode<=13)
            cur=(uint8_t)(config.Einkanal_mode-9);
        crsf.send_param_response_CRSF_TEXT_SELECTION(164,160,
            "Einkanal Mode","Normal;WM0;WM1;WM2;WM3",cur,0,4);
    }


    else if(idx==165) crsf.send_param_response_CRSF_TEXT_SELECTION(165,160,
        "Hardware Config","V1;V2;V3",
        (uint8_t)constrain(config.Hardware_Config_Pending,0,2),0,2);
    else if(idx==166) crsf.send_param_response_CRSF_UINT8(166,160,"PWM min (/16 us)",
        (uint8_t)(constrain(config.PWM_scale_min,0,4080)/16),0,255,"");
    else if(idx==167) crsf.send_param_response_CRSF_UINT8(167,160,"PWM max (/16 us)",
        (uint8_t)(constrain(config.PWM_scale_max,0,4080)/16),0,255,"");

    // ── Gruppenadressen fuer "MKan" ──────────────────────────────────────
    // 168-169: je eine der 2 Gruppenadressen (0-255, 255=Gruppe aus),
    // unabhaengig von "Modul Adresse" (162).
    else if(idx==168) crsf.send_param_response_CRSF_UINT8(168,160,"Gruppenadr. 1 (255=aus)",
        (uint8_t)constrain(config.EK_Gruppen_Adresse[0],0,255),0,255,"");
    else if(idx==169) crsf.send_param_response_CRSF_UINT8(169,160,"Gruppenadr. 2 (255=aus)",
        (uint8_t)constrain(config.EK_Gruppen_Adresse[1],0,255),0,255,"");

    // ── Geraetename (170) ─────────────────────────────────────────────────
    else if(idx==170) {
        snprintf(buf,sizeof(buf),"%s (Web aendern)",config.Device_Name);
        crsf.send_param_response_CRSF_INFO(170,0,"Geraetename",buf);
    }

    // ── Port B / Port C (171-173, Kind von Folder 160) ──────────────────
    // Optionsliste kommt aus der Port-B-Rollen-Registry (port_function.h/
    // .cpp), damit sie nicht hier UND im Web-UI doppelt gepflegt wird.
    else if(idx==171) crsf.send_param_response_CRSF_TEXT_SELECTION(171,160,
        "Port B Modus",portBFunctionOptionsString(),
        (uint8_t)constrain(config.PortB_Mode,0,(int)portBFunctionMaxIndex()),0,portBFunctionMaxIndex());
    else if(idx==172) crsf.send_param_response_CRSF_TEXT_SELECTION(172,160,
        "Port B Baudrate",PORTB_BAUD_LIST_STR,
        portBBaudToIndex(config.PortB_Baud),0,PORTB_BAUD_LIST_COUNT-1);
    else if(idx==173) crsf.send_param_response_CRSF_TEXT_SELECTION(173,160,
        "Port C (GPS)","Aus;An",
        config.PortC_GPS_Enabled?1:0,0,1);

    // ── Manueller WLAN-Schalter + Auto-Failsafe (174-176, Kinder von Folder
    // 160) ────────────────────────────────────────────────────────────────
    // 174 wirkt SOFORT (siehe crsfWriteParam) und wird bewusst NICHT
    // persistiert - wie das "Test Sound"-Feld (sub==5 weiter oben), das
    // ebenfalls direkt statt ueber markDirty() wirkt. 175/176 sind normale,
    // persistierte Konfiguration.
    else if(idx==174) crsf.send_param_response_CRSF_TEXT_SELECTION(174,160,
        "WLAN jetzt","Aus;Ein",
        WebServerManager::isApActive()?1:0,0,1);
    else if(idx==175) crsf.send_param_response_CRSF_TEXT_SELECTION(175,160,
        "Auto-WLAN bei Signalverlust","Aus;Ein",
        config.WifiAutoEnable?1:0,0,1);
    else if(idx==176) crsf.send_param_response_CRSF_UINT8(176,160,"Auto-WLAN Timeout (s)",
        (uint8_t)constrain(config.WifiAutoTimeoutSec,5,240),5,240,"");

    // 181: "WLAN dauerhaft an" - Default AN, ueberstimmt 174/175/176
    // vollstaendig (siehe wifiFailsafeCheck()/setup()). Bewusst als
    // eigenstaendiges, neu angehaengtes Feld statt Umbau von 174-176, damit
    // bestehende Radio-Profile/Lua-Skripte mit festen Feld-IDs weiterlaufen.
    else if(idx==181) crsf.send_param_response_CRSF_TEXT_SELECTION(181,160,
        "WLAN dauerhaft an","Aus;Ein",
        config.WifiAlwaysOn?1:0,0,1);

    // ── SBUS-Gruppenkanaele fuer "MKan" (SBUS-Pendant zu 168/169) ────────
    // 177/179: je ein fest zugewiesener SBUS-Kanal (255=aus) fuer eine der
    // beiden Gruppen; 178/180: Modus auf diesem Kanal - dieselbe Werte-/
    // Anzeigekonvention wie "Einkanal Mode" (164): Normal (0) oder eine der
    // 4 WM-Adressen (10-13), siehe einkanalFunctionSBUSGroup() oben.
    else if(idx==177) {
        uint8_t v=(config.SBUS_Gruppen_Channel[0]==999)?255:(uint8_t)constrain(config.SBUS_Gruppen_Channel[0],0,15);
        crsf.send_param_response_CRSF_UINT8(177,160,"SBUS Gruppe1 Kanal (255=aus)",v,0,255,"");
    }
    else if(idx==178) {
        uint8_t cur=0;
        if(config.SBUS_Gruppen_Mode[0]==0)cur=0;
        else if(config.SBUS_Gruppen_Mode[0]>=10&&config.SBUS_Gruppen_Mode[0]<=13)
            cur=(uint8_t)(config.SBUS_Gruppen_Mode[0]-9);
        crsf.send_param_response_CRSF_TEXT_SELECTION(178,160,
            "SBUS Gruppe1 Modus","Normal;WM0;WM1;WM2;WM3",cur,0,4);
    }
    else if(idx==179) {
        uint8_t v=(config.SBUS_Gruppen_Channel[1]==999)?255:(uint8_t)constrain(config.SBUS_Gruppen_Channel[1],0,15);
        crsf.send_param_response_CRSF_UINT8(179,160,"SBUS Gruppe2 Kanal (255=aus)",v,0,255,"");
    }
    else if(idx==180) {
        uint8_t cur=0;
        if(config.SBUS_Gruppen_Mode[1]==0)cur=0;
        else if(config.SBUS_Gruppen_Mode[1]>=10&&config.SBUS_Gruppen_Mode[1]<=13)
            cur=(uint8_t)(config.SBUS_Gruppen_Mode[1]-9);
        crsf.send_param_response_CRSF_TEXT_SELECTION(180,160,
            "SBUS Gruppe2 Modus","Normal;WM0;WM1;WM2;WM3",cur,0,4);
    }
}

// ======== CRSF: Parameter schreiben ==================================
static void crsfWriteParam(uint8_t idx, uint8_t val) {
    if     (idx==3) {config.throttle_mode=constrain(val,0,1);markDirty();}
    else if(idx==4) {config.engine_on_toggle=constrain(val,0,1);markDirty();}
    else if(idx==5) {config.Source_Start_Sound[0]=einSelWrite(val,config.Source_Start_Sound[0]);markDirty();}
    else if(idx==6) {uint8_t t=einTyp(config.Source_Start_Sound[0]);config.Source_Start_Sound[0]=setEin(t,val);markDirty();}
    else if(idx==7) {
        // V3: val 0=BUS,1=Deakt  V1/V2: val 0=BUS,1=PWM,2=Deakt
        uint8_t nr=spdNr(config.Source_Speed_Sound_0);
        uint8_t realTyp=(config.Hardware_Config>=2&&val==1)?2:val;
        config.Source_Speed_Sound_0=setSpd(realTyp,nr); markDirty();
    }
    else if(idx==8) {config.Source_Speed_Sound_0=setSpd(spdTyp(config.Source_Speed_Sound_0),val);markDirty();}
    else if(idx==9) {config.Volumen_Sound[0]=constrain(val,0,200);markDirty();}
    else if(idx==10){config.Min_Speed_Sound_0=constrain(val,0,200);markDirty();}
    else if(idx==11){config.Max_Speed_Sound_0=constrain((int)val*2,100,510);markDirty();}
    else if(idx==12){config.shutdowndelay=constrain(val,0,60);markDirty();}
    else if(idx==13){config.throttle_ramp=constrain(val,0,50);markDirty();}
    else if(idx==14){config.throttle_dead_band=constrain(val,0,50);markDirty();}
    else if(idx>=16&&idx<=159) {
        uint8_t s=(idx-16)/6+1, sub=(idx-16)%6;
        if(s<1||s>24)return;
        if     (sub==1){config.Source_Start_Sound[s]=einSelWrite(val,config.Source_Start_Sound[s]);markDirty();}
        else if(sub==2){config.Source_Start_Sound[s]=setEin(einTyp(config.Source_Start_Sound[s]),val);markDirty();}
        else if(sub==3){config.Volumen_Sound[s]=constrain(val,0,200);markDirty();}
        else if(sub==4){config.Mode_Sound[s]=constrain(val,0,2);markDirty();}
        else if(sub==5){if(val==1)Sound_on_web[s]=true;testSoundActive[s]=(val==1);}
    }
    else if(idx==161){config.Einkanal_RC_System=constrain(val,0,4);markDirty();}
    else if(idx==162){config.modul_adress=constrain(val,0,20);crsf.setDeviceAddress(crsfAddrFromWM());markDirty();}
    else if(idx==163){config.Einkanal_Channel=(val==255)?999:constrain(val,0,15);markDirty();}
    else if(idx==164){config.Einkanal_mode=(val==0)?0:constrain((int)val+9,10,13);markDirty();}
    else if(idx==165){config.Hardware_Config_Pending=constrain(val,0,2);markDirty();
                     Serial.printf("!!! Hardware Config -> %d (wirkt erst nach Neustart) – NEUSTART ERFORDERLICH !!!\n",config.Hardware_Config_Pending);
                     // config.Hardware_Config selbst bleibt bis zum Neustart unveraendert
                     // (siehe loadConfig() in config.cpp) - S.Port/GPS/Pins duerfen
                     // sich waehrend des Betriebs nicht unter den bereits laufenden
                     // Initialisierungen aendern.
                     }
    else if(idx==166){config.PWM_scale_min=constrain((int)val*16,0,4080);markDirty();}
    else if(idx==167){config.PWM_scale_max=constrain((int)val*16,0,4080);markDirty();}
    else if(idx==168){config.EK_Gruppen_Adresse[0]=constrain(val,0,255);markDirty();}
    else if(idx==169){config.EK_Gruppen_Adresse[1]=constrain(val,0,255);markDirty();}
    // ── Port B / Port C - wirkt wie Hardware Config (165) erst nach einem
    // Neustart: Port B/GPS werden nur einmal in setup() initialisiert
    // (siehe PortB_Mode_boot/PortC_GPS_Enabled_boot) - ein Live-Umschalten
    // des UART-Modus waehrend des Betriebs waere ein Risiko fuer den
    // bereits laufenden Bus.
    else if(idx==171){
        uint8_t m=constrain(val,0,(int)portBFunctionMaxIndex());
        if (m!=config.PortB_Mode) {
            // Baudrate beim Wechsel des Modus auf den protokolltypischen
            // Standard vorbelegen (kommt aus der Port-B-Rollen-Registry, kein
            // doppelt gepflegtes switch/case) - bleibt danach ueber Feld 172
            // frei aenderbar (im Web-UI ohnehin immer frei editierbar).
            // 0 = PORTB_OFF -> Baudrate unveraendert lassen.
            uint32_t defBaud = portBFunctionDefaultBaud((PortBMode)m);
            if (defBaud != 0) config.PortB_Baud = defBaud;
        }
        config.PortB_Mode=m; markDirty();
        Serial.printf("!!! Port B Modus -> %d (wirkt erst nach Neustart) – NEUSTART ERFORDERLICH !!!\n",m);
    }
    else if(idx==172){
        uint8_t i=constrain(val,0,(int)PORTB_BAUD_LIST_COUNT-1);
        config.PortB_Baud=portBBaudList[i]; markDirty();
        Serial.printf("!!! Port B Baudrate -> %u (wirkt erst nach Neustart) – NEUSTART ERFORDERLICH !!!\n",(unsigned)config.PortB_Baud);
    }
    else if(idx==173){
        config.PortC_GPS_Enabled=constrain(val,0,1);markDirty();
        Serial.printf("!!! Port C (GPS) -> %d (wirkt erst nach Neustart) – NEUSTART ERFORDERLICH !!!\n",config.PortC_GPS_Enabled);
    }
    // ── Manueller WLAN-Schalter (174) - wirkt SOFORT ueber enableAP()/
    // disableAP(), absichtlich OHNE markDirty()/Persistierung (wie "Test
    // Sound" oben): der naechste Bootvorgang soll ganz normal vom WifiPin/
    // GPIO13-Zustand abhaengen, nicht vom zuletzt am Sender gesetzten Wert.
    // 175/176 sind normale persistierte Konfiguration fuer den Auto-Failsafe
    // (siehe wifiFailsafeCheck()).
    else if(idx==174){
        if (constrain(val,0,1)) WebServerManager::enableAP();
        else                    WebServerManager::disableAP();
    }
    else if(idx==175){
        config.WifiAutoEnable=constrain(val,0,1);markDirty();
    }
    else if(idx==176){
        config.WifiAutoTimeoutSec=(uint16_t)constrain((int)val,5,240);markDirty();
    }
    else if(idx==177){config.SBUS_Gruppen_Channel[0]=(val==255)?999:constrain(val,0,15);markDirty();}
    else if(idx==178){config.SBUS_Gruppen_Mode[0]=(val==0)?0:constrain((int)val+9,10,13);markDirty();}
    else if(idx==179){config.SBUS_Gruppen_Channel[1]=(val==255)?999:constrain(val,0,15);markDirty();}
    else if(idx==180){config.SBUS_Gruppen_Mode[1]=(val==0)?0:constrain((int)val+9,10,13);markDirty();}
    // 181: "WLAN dauerhaft an" - rein persistierte Einstellung wie 175/176,
    // KEIN sofortiges enableAP() (anders als der Sofort-Schalter 174).
    // Wirkt erst ab dem naechsten Bootvorgang. Bewusst so (seit v7.15) - ein
    // WLAN-Start zur Laufzeit ist historisch die fehleranfaelligste
    // Operation in diesem Projekt (Heap-Fragmentierung), ein Aufruf direkt
    // aus dem CRSF-Parameter-Handler heraus ist dafuer unnoetiges Risiko,
    // da der AP durch den Default "an" beim Booten ohnehin schon laeuft.
    else if(idx==181){
        config.WifiAlwaysOn=constrain(val,0,1);markDirty();
    }
}

// ======== Setup ======================================================
void setup() {
    Serial.begin(115200);
    loadConfig();

    strncpy(ssid,    config.WiFi_SSID,    sizeof(ssid)-1);    ssid[sizeof(ssid)-1]='\0';
    strncpy(password,config.WiFi_Password,sizeof(password)-1); password[sizeof(password)-1]='\0';

    // CRSF oder SBUS starten
    if (config.Einkanal_RC_System == 4) {
        crsf.init_crsf(&Serial2, 16, 17);
        crsf.setDeviceAddress(crsfAddrFromWM());   // eindeutige CRSF-Adresse aus WM-Adresse
        Serial.printf("CRSF (RX=16, TX=17)  Geraeteadresse 0x%02X  Slot %d\n",
                      crsfAddrFromWM(), (uint8_t)constrain(config.modul_adress,0,15) * 2);
    } else {
        sbus_rx.Begin();
        Serial.println("SBUS gestartet");
    }

    // GPIO-Pins je nach Hardware-Version
    switch (config.Hardware_Config) {
        case 0: { uint8_t p[]={16,17,22, 0, 2, 4}; memcpy(Input_Pin,p,6); break; }
        case 1: { uint8_t p[]={16,17,14,27,32,33}; memcpy(Input_Pin,p,6); break; }
        default: break;  // V3/V4: Input_Pin nicht verwendet
    }

    pinMode(WifiPin, INPUT_PULLUP);

    // INPUT_PULLUP für Eingangs-Pins (V1/V2 only, GPIO16/17 bei CRSF überspringen)
    if (config.Hardware_Config < 2) {
        for (uint8_t i = 2; i < 6; i++) {
            if (config.Einkanal_RC_System == 4 &&
                (Input_Pin[i] == 16 || Input_Pin[i] == 17)) continue;
            pinMode(Input_Pin[i], INPUT_PULLUP);
        }
    }
    // CRSF RX/TX-Pins nie als INPUT_PULLUP (gilt für alle Versionen)
    // GPIO16/17 werden durch crsf.init_crsf() / sbus_rx.Begin() konfiguriert

    SDCardInit();
    LoadFiles();

    // begin() registriert nur die HTTP-Routen und merkt sich SSID/Passwort -
    // das passiert IMMER, unabhaengig vom WifiPin-Zustand beim Booten, weil
    // WLAN auch spaeter zur Laufzeit (manueller Schalter oder Auto-Failsafe)
    // gestartet werden koennen muss. Das eigentliche Einschalten von AP-Funk
    // + Webserver-Socket passiert davon getrennt, nur bedingt, ueber
    // enableAP() (siehe WebServerManager.h/.cpp).
    WebServerManager::begin(ssid, password);

    // "WLAN dauerhaft an" (config.WifiAlwaysOn, Default AN, siehe config.h)
    // schaltet den AP unconditional ein und ueberstimmt damit den
    // GPIO13-Bootpin komplett - wer den Pin-Bootmodus weiterhin will, muss
    // die Option zuerst explizit ausschalten (Web-Tab "WiFi" oder CRSF/Lua-
    // Feld 181).
    if (config.WifiAlwaysOn) {
        Serial.println("WLAN dauerhaft aktiv (config.WifiAlwaysOn) - schalte AP ein...");
        WebServerManager::enableAP();
    } else if (!digitalRead(WifiPin)) {
        Serial.println("AP Modus (WifiPin beim Booten auf LOW)...");
        WebServerManager::enableAP();
    }

    // Der SD-Hintergrund-Lese-Task startet bewusst erst HIER, NACH dem
    // WLAN-AP-Start (siehe XT_I2S_Audio.cpp) - nicht schon im Konstruktor
    // des globalen I2SAudio-Objekts, also nicht vor setup()/WiFi-Start.
    I2SAudio.StartSdReaderTask();

    // Reserviert die Sample-Puffer aller Sound-Objekte (siehe
    // AllocateSoundBuffers()/XT_I2S_Audio.h) ebenfalls erst HIER, NACH dem
    // WLAN-AP-Start, statt sie als feste .bss-Arrays seit Programmstart
    // mitzuschleppen - WiFi.softAP() laeuft dadurch auf einem moeglichst
    // freien/unfragmentierten Heap.
    AllocateSoundBuffers();

    Einkanal_RC_System_boot = config.Einkanal_RC_System;
    PortB_Mode_boot         = config.PortB_Mode;
    PortB_Baud_boot         = config.PortB_Baud;
    PortC_GPS_Enabled_boot  = config.PortC_GPS_Enabled;
    sprintf(versionString, "%d.%02d", Version/100, Version%100);
    saveConfig();

    // Port B (GPIO32/33, echter 2. Hardware-UART): setup() liest nur aus der
    // Port-B-Rollen-Registry (port_function.h/.cpp), welches Modul fuer die
    // per PortB_Mode_boot gewaehlte Rolle zustaendig ist, statt ein
    // rollenspezifisches switch/case zu pflegen.
    {
        const PortBFunctionModule* portBModule = getPortBModule((PortBMode)PortB_Mode_boot);
        if (portBModule != nullptr) {
            HardwareSerial* portBSerial = portBFreeSerial();
            // Kollisionsschutz: portBFreeSerial() DARF NIEMALS denselben
            // Hardware-UART liefern, den Port A (der Haupt-RC-Bus) bereits
            // aktiv nutzt - sonst wuerden sich beide Ports denselben UART
            // teilen und sich gegenseitig stoeren.
            HardwareSerial* portASerial = (config.Einkanal_RC_System == 4) ? &Serial2 : &Serial1;
            if (portBSerial == portASerial) {
                Serial.println("!!! Port B: kein freier Hardware-UART verfuegbar (Kollision mit Port A/Haupt-Bus) - Port B bleibt AUS !!!");
            } else if (portBModule->isActive != nullptr && portBModule->isActive()) {
                // Idempotenz-Absicherung: ein Modul, das sich bereits selbst
                // als aktiv meldet, wird nicht ein zweites Mal initialisiert
                // (begin() koennte sonst z.B. serial->begin() doppelt aufrufen).
                Serial.println("!!! Port B: Rolle ist bereits aktiv, ueberspringe erneute Initialisierung !!!");
            } else if (portBModule->begin != nullptr) {
                portBModule->begin(portBSerial, 32, 33, PortB_Baud_boot);
            }
        }
        // PORTB_OFF (oder ein unbekannter/ungueltiger Wert) -> getPortBModule()
        // liefert nullptr, Port B bleibt unangetastet/aus.
    }

    // Port C (GPS) - unabhaengiger An/Aus-Schalter. Kollidiert mit SBUS als
    // Bus-Protokoll (beide belegen GPIO27), siehe gpsPinConflict.
    if (PortC_GPS_Enabled_boot) {
        if (config.Einkanal_RC_System != 4) {
            gpsPinConflict = true;
            Serial.println("!!! WARNUNG: Port C (GPS) zusammen mit SBUS gewaehlt !!!");
            Serial.println("!!! GPIO27 wird von SBUS UND GPS gleichzeitig belegt -> GPS bleibt deaktiviert !!!");
            Serial.println("!!! Fuer GPS-Speed bitte Einkanal/RC-System auf CRSF (4) stellen. !!!");
        } else {
            gpsSpeedInit();
        }
    }

    Serial.printf("ESP32-RC-Sound %s – Hardware: %s\n", versionString, hwVersionStr());
}

// Auto-WLAN-Failsafe, angelehnt an ELRS-Empfaenger: schaltet das WLAN
// automatisch ein, wenn eine Zeit lang kein gueltiges RC-Signal anliegt -
// z.B. wenn das Modell auf der Werkbank liegt und der Sender aus ist. Nutzt
// den bereits vorhandenen, fuer CRSF UND SBUS gemeinsam gepflegten BUS_OK-
// Flag (siehe checkCrsfTimeout()/sbus_data.failsafe) statt einer zweiten,
// eigenen Signalverlust-Erkennung. Schaltet NUR EIN, nie automatisch wieder
// aus: einmal aktiviertes WLAN bleibt an bis zum manuellen Ausschalten
// (Web/Lua) oder Neustart - ein automatisches Wieder-Abschalten wuerde eine
// laufende Konfiguration/ein laufendes OTA-Update abwuergen, sobald kurz
// wieder ein gueltiges RC-Signal hereinkaeme. config.WifiAutoEnable/
// WifiAutoTimeoutSec siehe config.h/.cpp.
// Ein WLAN-Start zur Laufzeit (statt wie beim Booten VOR
// AllocateSoundBuffers()) laeuft mit potenziell staerker fragmentiertem
// Heap - historisch die fehleranfaelligste Operation in dieser Codebase.
// enableAP() loggt deshalb bei jedem Aufruf den freien Heap vor dem Start.
//
// "WLAN dauerhaft an" (config.WifiAlwaysOn, siehe config.h/setup()) schaltet
// den AP bereits beim Booten fest ein - diese Funktion greift dann gar nicht
// mehr ein, siehe Abbruch ganz oben.
void wifiFailsafeCheck() {
    static unsigned long busLostSinceMs = 0;
    static bool          busLostSincePending = false;

    if (config.WifiAlwaysOn) {
        // WLAN ist per Konfiguration dauerhaft an (siehe setup()) - hier ist
        // nichts mehr zu tun, unabhaengig vom RC-Signal.
        busLostSincePending = false;
        return;
    }
    if (BUS_OK) {
        busLostSincePending = false;
        return;
    }
    if (!config.WifiAutoEnable) {
        busLostSincePending = false;
        return;
    }
    if (WebServerManager::isApActive()) {
        // WLAN laeuft bereits (manuell oder schon vom Failsafe gestartet) -
        // nichts zu tun, siehe Kommentar oben ("nie automatisch wieder aus").
        busLostSincePending = false;
        return;
    }
    if (!busLostSincePending) {
        busLostSincePending = true;
        busLostSinceMs = millis();
        return;
    }
    unsigned long timeoutMs = (unsigned long)config.WifiAutoTimeoutSec * 1000UL;
    if (millis() - busLostSinceMs >= timeoutMs) {
        Serial.printf("Auto-WLAN-Failsafe: %u s ohne gueltiges RC-Signal - schalte WLAN ein.\n",
                      (unsigned)config.WifiAutoTimeoutSec);
        WebServerManager::enableAP();
    }
}

// ======== Loop =======================================================
void loop() {
    currentTime = millis();
    if (WebServerManager::isApActive()) WebServerManager::Webpage();

    // Port B - Boot-Snapshot verwenden (siehe PortB_Mode_boot), generischer
    // Registry-Aufruf statt rollenspezifischem if/else if.
    {
        const PortBFunctionModule* portBModule = getPortBModule((PortBMode)PortB_Mode_boot);
        if (portBModule != nullptr && portBModule->update != nullptr) portBModule->update();
    }
    // Port C (GPS) laufend einlesen (nicht bei GPIO27-Konflikt, siehe setup())
    if (PortC_GPS_Enabled_boot && !gpsPinConflict) {
        gpsSpeedUpdate();
    }

    if (Einkanal_RC_System_boot == 4) {
        crsf.read_packets(0);
        for (int i=0;i<16;i++) channel_output[i]=crsf.get_crfs_channels(i);
        // BUS_OK ist daran gekoppelt, ob gerade ein gueltiger CRSF-Frame
        // geparst wurde (validFramesRx-Zaehler erhoeht) - nicht an
        // channel_output[0]>0, denn ein Kanalwert allein ist kein
        // zuverlaessiger Nachweis fuer einen frischen Frame (z.B. bei
        // legitim niedrigem Kanal-1-Wert).
        static uint32_t lastValidFrameCount = 0;
        uint32_t vf = crsf.getValidFrames();
        if (vf != lastValidFrameCount) { lastValidFrameCount = vf; BUS_OK = true; lastCrsfPacket = millis(); }
        checkCrsfTimeout();

        static unsigned long lastTelem = 0;
        if (millis() - lastTelem >= 100) {
            lastTelem = millis();

            // Nur Einzelzellen-Telemetrie (0x0E) senden, wenn LiPo-Sensoren vorhanden.
            // KEIN Batterie-Frame (0x08): dadurch bleibt RxBt die vom Empfaenger (HR8E)
            // selbst gemessene Spannung und wird nicht mit 0 ueberschrieben.
            // Das Modul erscheint im TBS Agent trotzdem, weil es auf jeden Ping mit
            // device_info antwortet – die Geraete-Erkennung laeuft NICHT ueber Telemetrie.
            if (PortB_Mode_boot == PORTB_SPORT) {
                if (lipoSensor[0].online) sportSendCellsTelemetry(0);
                if (lipoSensor[1].online) sportSendCellsTelemetry(1);
            }
            // GPS-Telemetrie (CRSF-GPS-Frame) -> EdgeTX zeigt "GSpd"
            if (PortC_GPS_Enabled_boot) {
                uint16_t gspd = (uint16_t)(gpsGetSpeedKmh() * 10.0f); // CRSF: km/h * 10
                crsf.send_tele_GPS(gpsGetLatE7(), gpsGetLonE7(), gspd,
                                   gpsGetHeadingCdeg(), gpsGetAltMeters(), gpsGetSatellites());
            }
        }
        if(crsf.getDeviceInfoReplyPending() && (millis() - crsf.getPingTime() >= crsfSlotDelayMs())){
            // Erst im eigenen Zeit-Slot auf den Broadcast-Ping antworten (Wilhelm-Meier-Schema),
            // damit sich die Device-Info-Antworten mehrerer Module nicht ueberlappen.
            crsf.setDeviceInfoReplyPending(false);
            char dn[24]; snprintf(dn,sizeof(dn),"%s@%d",config.Device_Name,config.modul_adress);
            crsf.send_device_info(dn,CRSF_PARAM_COUNT);
        }
        if(crsf.getDeviceReadReplyPending()){
            uint8_t pi=crsf.getParamReadIndex();
            crsf.setDeviceReadReplyPending(false);
            crsfSendParam(pi);
        }
        if(crsf.getDeviceWriteReplyPending()){
            uint8_t wi=crsf.getParamWriteIndex(),wv=crsf.getParamWriteValue();
            crsf.setDeviceWriteReplyPending(false);
            crsfWriteParam(wi,wv); crsfSendParam(wi);
        }
        if(crsf.getDeviceCommandReplyPending()){
            crsf.setDeviceCommandReplyPending(false);
            einkanalFunctionCRSF();
        }
    } else {
        if(sbus_rx.Read()){
            sbus_data=sbus_rx.data(); BUS_OK=true;
            if(!sbus_data.failsafe){
                if(config.Einkanal_Channel<16) einkanalFunction(sbus_data.ch[config.Einkanal_Channel]);
                // "MKan"-Gruppen ueber SBUS (siehe einkanalFunctionSBUSGroup()) -
                // je Gruppe ein eigener, fest zugewiesener Kanal (999 = aus).
                for(uint8_t g=0; g<RCSOUND_NUM_GROUPS; g++){
                    int c = config.SBUS_Gruppen_Channel[g];
                    if(c>=0 && c<16) einkanalFunctionSBUSGroup(g, sbus_data.ch[c]);
                }
            } else BUS_OK=false;
            for(int i=0;i<16;i++) channel_output[i]=sbus_data.ch[i];
        }
    }

    // NVS-Speicher: 500 ms nach der letzten Aenderung schreiben. Kurz genug, dass
    // eine Aenderung ein Ausschalten praktisch immer "ueberlebt" (frueher 2000 ms ->
    // Aenderung ging bei schnellem Ausschalten verloren), aber lang genug, dass
    // schnelle Schieberegler-Bursts weiterhin zu einem einzigen Schreibvorgang
    // gebuendelt werden (der Timer wird bei jeder Aenderung zurueckgesetzt).
    // v7.16: zusaetzlich zurueckgehalten, solange seit einer SD-Schreib-
    // aktivitaet (Sound-Upload/-Loeschen) noch der Cooldown laeuft (siehe
    // WebServerManager::sdActivityCooldownActive()) - Feldbefund: ohne
    // Stuetzkondensator am Modul kann ein sofortiger blockierender NVS-
    // Schreibvorgang direkt nach einem SD-Schreibvorgang die Versorgung so
    // weit einbrechen lassen, dass ein Brownout-Reset-Loop entsteht. Sowohl
    // die Web-Speichern-Buttons (handleApiConfigPost()/"/save"-Route in
    // WebServerManager.cpp) als auch das CRSF/Lua-Menue markieren nur noch
    // "dirty" statt sofort zu schreiben - dieser Check hier ist seitdem die
    // EINZIGE Stelle, an der saveConfigForce() waehrend des Betriebs
    // tatsaechlich aufgerufen wird (Ausnahme: saveConfig() einmalig am Ende
    // von setup()).
    if(configDirty&&(millis()-configDirtyMs)>=500UL&&!WebServerManager::sdActivityCooldownActive())
        saveConfigForce();

    // Auto-WLAN-Failsafe (siehe wifiFailsafeCheck()) - BUS_OK ist an dieser
    // Stelle im loop() fuer diesen Durchlauf bereits aktuell (CRSF-Zweig
    // setzt/prueft es oben ueber checkCrsfTimeout(), SBUS-Zweig direkt beim
    // Read()).
    wifiFailsafeCheck();

    Config();
    if(currentTime<5000) return;
    for(uint8_t i=1;i<=24;i++) handleSound(i,Sounds[i]);

    // Motor Toggle
    if(Sound_on[0]&&!Sound_on_Motor_state&&config.engine_on_toggle){Sound_on_Motor=!Sound_on_Motor;Sound_on_Motor_state=true;}
    else if(!Sound_on[0]&&Sound_on_Motor_state&&config.engine_on_toggle) Sound_on_Motor_state=false;
    else if(!config.engine_on_toggle) Sound_on_Motor=Sound_on[0];

    bool mw=Sound_on_Motor||Sound_on_web[0];
    if((mw&&!engine_break&&!Sound_play[0])||(throttle>0&&engine_break&&!Sound_play[0])||(throttle>0&&Sound_play[0])){Sound_play[0]=true;shutdown_timer=millis();}
    if(!mw){Sound_play[0]=false;engine_break=false;}
    if(config.shutdowndelay>0&&Sound_play[0]&&(millis()-shutdown_timer)>((unsigned long)config.shutdowndelay*1000UL)){Sound_play[0]=false;engine_break=true;}

    switch(engine_State){
        case OFF:    if(Sound_play[0]){Sound_start.Volume=config.Volumen_Sound[0];I2SAudio.Play(&Sound_start);engine_State=STARTING;} break;
        case STARTING: if(!Sound_start.Playing){throttle=0;last_throttle=0;Sound_loop.RepeatForever=true;Sound_loop.Volume=config.Volumen_Sound[0];I2SAudio.Play(&Sound_loop);engine_State=RUNNING;} break;
        case RUNNING:  Sound_loop.Volume=config.Volumen_Sound[0]; if(!Sound_play[0]){throttle=0;if(last_throttle==0){Sound_loop.RepeatForever=false;I2SAudio.Stop(&Sound_loop);engine_State=STOPPING;}} break;
        case STOPPING: if(!Sound_loop.Playing){Sound_shut.Volume=config.Volumen_Sound[0];I2SAudio.Play(&Sound_shut);engine_State=OFF;} break;
    }
    throttle=ramp_throttle(throttle);
    if(config.Min_Speed_Sound_0!=config.Max_Speed_Sound_0)
        Sound_loop.Speed=floatMap(throttle,0,100,config.Min_Speed_Sound_0,config.Max_Speed_Sound_0)/100.0f;
}

// ======== Sound ======================================================
void handleSound(uint8_t idx, XT_Wav_Class* snd) {
    if(!snd)return;
    if(Sound_on[idx]||Sound_on_web[idx]){
        snd->Volume=config.Volumen_Sound[idx];
        if(!Sound_play[idx]){Sound_play[idx]=true;snd->LoadWavFile();if(config.Mode_Sound[idx]==1)snd->RepeatForever=true;I2SAudio.Play(snd);Sound_on_web[idx]=false;}
    }else{
        snd->RepeatForever=false;
        // NEU v7.17: Lautstaerke auch hier weiter aktualisieren, solange der
        // Sound noch nachspielt (Sound_play[idx] true) - z.B. beim Web-
        // Testbutton (testSound()), der Sound_on_web[idx] bereits im selben
        // Frame wieder zuruecksetzt, in dem die Wiedergabe gestartet wird
        // (siehe oben). Ohne dies wirkte eine waehrend einer laufenden
        // Testwiedergabe im Web-UI geaenderte und gespeicherte Lautstaerke
        // erst beim naechsten Trigger, nicht auf die bereits laufende
        // Wiedergabe (Feldbefund, siehe README-Changelog v7.17). Der Motor-
        // Sound (idx 0, eigene Zustandsmaschine im loop()) war davon nie
        // betroffen - dort wird die Lautstaerke ohnehin bei jedem
        // loop()-Durchlauf im Zustand RUNNING neu gesetzt.
        if(Sound_play[idx]) snd->Volume=config.Volumen_Sound[idx];
        if(config.Mode_Sound[idx]==2&&Sound_play[idx])I2SAudio.Stop(snd);
        // testSoundActive[idx] (Anzeige "Test Sound" im CRSF-/Lua-Menue,
        // crsfWriteParam sub==5) wird hier zurueckgesetzt, sobald das Ende
        // der Wiedergabe erkannt wird - der Web-Testbutton zeigt ohnehin
        // keinen dauerhaften Zustand. Gilt nicht fuer Loop-Mode
        // (RepeatForever): dort spielt der Sound
        // bewusst endlos weiter, "Ein" bleibt in dem Fall zurecht stehen.
        if(!snd->Playing&&Sound_play[idx]){snd->UnLoadWavFile();Sound_play[idx]=false;testSoundActive[idx]=false;}
    }
}

// ======== Config: Quellen auswerten ==================================
void Config() {
    bool isV3 = (config.Hardware_Config >= 2);  // V3 hat keine GPIO-Eingaenge (einziger Wert >=2)

    for (int x=0; x<=24; x++) {
        int src = config.Source_Start_Sound[x];
        if      (src>=0   && src<=15)  Sound_on[x] = BUS_OK && (channel_output[src] < 624);
        else if (src>=20  && src<=35)  Sound_on[x] = BUS_OK && (channel_output[src-20] > 1424);
        else if (src>=40  && src<=45)  {
            // PWM Low – nur V1/V2
            if (!isV3) { uint8_t pi=src-40; attachPWM_ISR(pi); unsigned int d=PWM_pulse_width[pi]; Sound_on[x]=pwmValid(pi)&&(d<2600)&&(map(d,config.PWM_scale_min,config.PWM_scale_max,0,100)<25); }
            else Sound_on[x]=false;
        }
        else if (src>=50  && src<=55)  {
            // PWM High – nur V1/V2
            if (!isV3) { uint8_t pi=src-50; attachPWM_ISR(pi); unsigned int d=PWM_pulse_width[pi]; Sound_on[x]=pwmValid(pi)&&(d<2600)&&(map(d,config.PWM_scale_min,config.PWM_scale_max,0,100)>75); }
            else Sound_on[x]=false;
        }
        else if (src>=60  && src<=65)  {
            // GPIO Pin – nur V1/V2
            if (!isV3) { uint8_t pi=src-60; Sound_on[x]=(pi<6)&&!digitalRead(Input_Pin[pi]); }
            else Sound_on[x]=false;
        }
        else if (src>=70  && src<=77)  Sound_on[x] = BUS_OK && bitRead(einkanal_Data, src-70);
        else if (src>=80  && src<=95) {
            // "MKan": 16 unabhaengige Einzelkanaele aus 2 Gruppen (je 8
            // Schalter, 1 Bit/Schalter = an/aus). Bei CRSF kommen die
            // Gruppen aus 2 frei waehlbaren Busadressen (siehe
            // einkanalFunctionCRSF()/updateGroupFromAddress()), bei SBUS
            // aus 2 fest zugewiesenen Kanaelen (siehe
            // einkanalFunctionSBUSGroup()) - beide fuellen gemeinsam
            // einkanal_Flat16. Jeder der 16 Werte ist ein eigener, direkt
            // gelesener Kanal.
            // nr 0..15  ->  Gruppe = nr/8 (0..1), Bit = nr%8 (0..7)
            uint8_t nr = (uint8_t)(src - 80);
            Sound_on[x] = BUS_OK && bitRead(einkanal_Flat16, nr);
        }
        else if (src==200)             Sound_on[x] = true;
        else                           Sound_on[x] = false;
    }

    // Motor Speed
    int spd = config.Source_Speed_Sound_0;
    if      (spd>=0 && spd<=15)        throttle = map(channel_output[spd], 82, 1900, 0, 100);
    else if (!isV3 && spd>=20 && spd<=25) { uint8_t pi=spd-20; attachPWM_ISR(pi); throttle = pwmValid(pi) ? map((long)PWM_pulse_width[pi], config.PWM_scale_min, config.PWM_scale_max, 0, 100) : 0; }
    else                               throttle = 0;
    throttle = constrain(throttle, 0, 100);

    if (config.throttle_mode) {
        if      (throttle > 50+config.throttle_dead_band) throttle=map((long)throttle,50+config.throttle_dead_band,100,0,100);
        else if (throttle < 50-config.throttle_dead_band) throttle=map((long)throttle,50-config.throttle_dead_band,0,0,100);
        else throttle=0;
    } else {
        throttle=(throttle>config.throttle_dead_band)?map((long)throttle,config.throttle_dead_band,100,0,100):0;
    }
    throttle = constrain(throttle, 0, 100);
}

// ======== Hilfsfunktionen ============================================
float floatMap(float x,float a,float b,float c,float d){if(b==a)return c;return(x-a)*(d-c)/(b-a)+c;}
// true nur, wenn der Kanal innerhalb PWM_TIMEOUT_MS einen frischen Puls
// gesehen hat - verhindert, dass ein abgerissenes/eingefrorenes PWM-Signal
// mit seinem letzten Wert weiter als "gueltig" durchgeht.
static inline bool pwmValid(uint8_t idx) {
    if (idx >= 6) return false;
    noInterrupts(); unsigned long last = PWM_lastPulseMs[idx]; interrupts();
    return (last != 0) && (millis() - last < PWM_TIMEOUT_MS);
}
// WICHTIG - max_files NICHT erhoehen (Erfahrung aus v7.07..v7.10):
// Hier stand voruebergehend SD.begin(SD_CS, SPI, 4000000, "/sd", 15), um das
// vfs_fat-Limit gleichzeitig offener Dateien (Standardwert max_files=5)
// anzuheben - wegen "vfs_fat: open: no free file descriptors" beim Upload.
// Das hat reproduzierbar das WLAN zerstoert: max_files=15 belegt beim Mounten
// dauerhaft einen deutlich groesseren Block im internen DRAM (je Datei ein
// FIL-Objekt inkl. 512-Byte-Sektorpuffer). Nicht die Kilobyte an sich sind
// das Problem, sondern dass dieser Block fest im Pool sitzt, BEVOR der
// WLAN-Treiber seine mehreren ZUSAMMENHAENGENDEN Puffer anfordert - daher
// "Expected to init 4 rx buffer, actual is 3" bzw. "rc_enable_trc fail,
// no mem" trotz >100 KB freiem Heap (Fragmentierung, kein Platzmangel).
// Belegt durch 7 von 7 Versionen: max_files=5 -> WLAN stabil (v7.03/05/06/11),
// max_files=15 -> WLAN defekt (v7.07/08/09/10).
// Der Deskriptor-Engpass wird stattdessen geloest, indem waehrend eines
// Uploads/Loeschens nicht benoetigte Handles FREIGEGEBEN werden - siehe
// releaseIdleMotorHandles() in WebServerManager.cpp.
void SDCardInit(){pinMode(SD_CS,OUTPUT);digitalWrite(SD_CS,HIGH);if(!SD.begin(SD_CS))Serial.println("FEHLER: SD!");}
void LoadFiles(){Sound_loop.LoadWavFile();Sound_shut.LoadWavFile();Sound_start.LoadWavFile();}

// Holt die Sample-Puffer ALLER Sound-Objekte vom Heap (siehe
// AllocateBuffers()/Kommentar in XT_I2S_Audio.h) - aufgerufen aus setup()
// NACH dem WLAN-AP-Start, damit WiFi.softAP() zuerst auf einem moeglichst
// freien/unfragmentierten Heap laeuft. Gefahrlos: LoadWavFile()/
// OpenWavFile() (vor dem WLAN-Start aufgerufen) lesen nur den WAV-Header in
// lokale Stack-Puffer, nicht in Samples/BackSamples - abgespielt
// (I2SAudio.Play()) wird ohnehin erst spaeter aus loop() heraus.
void AllocateSoundBuffers(){
    Sound_loop.AllocateBuffers();
    Sound_shut.AllocateBuffers();
    Sound_start.AllocateBuffers();
    for (int i = 1; i < RCSOUND_NUM_SLOTS; i++) {
        if (Sounds[i]) Sounds[i]->AllocateBuffers();
    }
}

float ramp_throttle(float tn){
    unsigned long rt=currentTime-previousTime_ramp; previousTime_ramp=currentTime;
    if(rt>1000)rt=1000; float step=(config.throttle_ramp/1000.0f)*(float)rt;
    if(tn>last_throttle){float s=last_throttle+step;tn=(s<tn)?s:tn;}
    else{float s=last_throttle-step;tn=(s>tn)?s:tn;}
    last_throttle=tn; return tn;
}

// ======== Einkanal SBUS ==============================================
// Decodiert die per Kanalwert "amplituden-codierte" WM-Adresse/Schalter/
// Status-Kombination (2 Bit Adresse, 3 Bit Schalter-Nr, 1 Bit Status) aus
// einem rohen SBUS-Kanalwert - Grundlage sowohl fuer den normalen Einkanal-
// WM-Modus (Einkanal_mode 10-13) unten als auch fuer die SBUS-Gruppen in
// einkanalFunctionSBUSGroup().
static void decodeSbusWm(uint16_t ch, uint8_t &addr, uint8_t &sw, uint8_t &st) {
    uint16_t n=0;uint8_t v=0;
    switch(config.Einkanal_RC_System){
        case 0:n=(ch>=172)?(ch-172+1):0;v=(uint8_t)(n>>4);break;
        case 1:n=(ch>=220)?(ch-220):0;v=(uint8_t)((n+(n>>6))>>4);break;
        case 2:n=(ch>=172)?(ch-172):0;v=(uint8_t)(n>>4);break;
        case 3:n=(ch>=205)?(ch-205):0;v=(uint8_t)(n>>4);break;
    }
    addr=(v>>4)&0b11; sw=(v>>1)&0b111; st=v&0b1;
}
void einkanalFunction(uint16_t ch) {
    einkanal_Data=ch;
    if(config.Einkanal_mode==0){einkanal_Data/=8;}
    else if(config.Einkanal_mode<=9){
        if(config.Einkanal_RC_System==0)einkanal_Data/=8;
        else if(config.Einkanal_RC_System==1){einkanal_Data=constrain(einkanal_Data,206,1837);einkanal_Data=((einkanal_Data-206)*10+20)/64;}
        else if(config.Einkanal_RC_System==2){float v=((float)einkanal_Data-172.0f+1.5f)*0.155677655677655f;einkanal_Data=(uint16_t)v;}
    }else{
        uint8_t addr,sw,st; decodeSbusWm(ch,addr,sw,st);
        if(addr==(uint8_t)(config.Einkanal_mode-10))bitWrite(einkanal_SpeicherWM,sw,st);
        einkanal_Data=einkanal_SpeicherWM;
    }
}

// ======== Einkanal-Gruppen SBUS (MKan-Aequivalent) =====================
// SBUS kennt - anders als CRSF - keine adressierten Pakete, daher braucht
// jede der beiden "MKan"-Gruppen hier einen eigenen, fest zugewiesenen
// SBUS-Kanal (config.SBUS_Gruppen_Channel[g], siehe loop()) statt einer
// frei waehlbaren Busadresse. config.SBUS_Gruppen_Mode[g] uebernimmt dabei
// dieselbe Werte-/Bedeutungskonvention wie config.Einkanal_mode beim
// einzelnen Einkanal-Kanal (siehe einkanalFunction() oben):
//   0      = "Normal"  - Kanalwert direkt (ch/8) als vollstaendiges 8-Bit-
//            Bitmuster fuer diese Gruppe uebernehmen, kein WM-Protokoll auf
//            diesem Kanal. Entspricht funktional dem "bits"-Wert, den
//            einkanalFunctionCRSF()/updateGroupFromAddress() aus einem
//            einzelnen CRSF-WM-Paket bekommt (dort liegt das volle 8-Bit-
//            Muster ja bereits fertig im Paket vor).
//   10-13  = "WM Adr 0..3" - derselbe amplituden-codierte WM-Modus wie beim
//            einzelnen Einkanal-Kanal (siehe decodeSbusWm() oben), da SBUS
//            hier (anders als "Normal") mehrere Schalter zeitversetzt ueber
//            denselben Kanal uebertraegt und Bit-fuer-Bit akkumuliert werden
//            muss.
// Das Ergebnis landet wie bei einkanalFunctionCRSF()/updateGroupFromAddress()
// in einkanal_Gruppe[g] und wird zu einkanal_Flat16 kombiniert - die
// "MKan"-Einzelkanaele (80-95) in Config() lesen CRSF- wie SBUS-Gruppen ueber
// denselben Flat16-Wert.
void einkanalFunctionSBUSGroup(uint8_t g, uint16_t ch) {
    if(config.SBUS_Gruppen_Mode[g]==0){
        einkanal_Gruppe[g]=(uint8_t)(ch/8);
    }else{
        uint8_t addr,sw,st; decodeSbusWm(ch,addr,sw,st);
        if(addr==(uint8_t)(config.SBUS_Gruppen_Mode[g]-10)) bitWrite(einkanal_SpeicherWM_Grp[g],sw,st);
        einkanal_Gruppe[g]=(uint8_t)einkanal_SpeicherWM_Grp[g];
    }
    einkanal_Flat16 = (uint32_t)einkanal_Gruppe[0] | ((uint32_t)einkanal_Gruppe[1] << 8);
}

// ======== Einkanal CRSF ==============================================
// Zusaetzlich zum Einkanal-Abgleich gegen die eigene Busadresse
// (config.modul_adress, Quelltyp 70-77) wird jede eingehende Schalter-
// Adresse auch gegen die 2 frei konfigurierbaren Gruppenadressen
// (config.EK_Gruppen_Adresse[]) geprueft. Trifft eine davon zu, landet der
// jeweilige 8-Bit-Wert (1 Bit/
// Schalter) in einkanal_Gruppe[g] und wird zu einkanal_Flat16 kombiniert -
// die Grundlage fuer die 16 "MKan"-Einzelkanaele (80-95) in Config().
static void updateGroupFromAddress(uint8_t addr, uint8_t bits){
    for(uint8_t g=0; g<RCSOUND_NUM_GROUPS; g++){
        if(config.EK_Gruppen_Adresse[g]!=255 && addr==(uint8_t)config.EK_Gruppen_Adresse[g]){
            einkanal_Gruppe[g]=bits;
        }
    }
    // Bewusst per Hand aufgezaehlt (2 Gruppen zu einem 16-Bit-Flachwert)
    // statt generisch ueber RCSOUND_NUM_GROUPS geloopt, damit ein Blick auf
    // diese Zeile sofort zeigt, wie viele Gruppen kombiniert werden - eine
    // kuenftige Aenderung von RCSOUND_NUM_GROUPS MUSS diese Zeile von Hand
    // mitziehen (siehe Kommentar bei RCSOUND_NUM_GROUPS in config.h).
    einkanal_Flat16 = (uint32_t)einkanal_Gruppe[0]
                     | ((uint32_t)einkanal_Gruppe[1] << 8);
}
void einkanalFunctionCRSF(){
    uint8_t wmc=crsf.get_cmd_buffer(5),cmd=crsf.get_cmd_buffer(6);
    if(wmc!=Multiswitch)return;
    switch(cmd){
        // MWset4: 8 Schalter, 2 Bit/Schalter (4 Zustaende) an einer Adresse.
        // einkanal_Data (Quelltyp 70-77) nutzt nur 1 Bit/Schalter aus
        // compressSwitches() und bleibt auf config.modul_adress bezogen.
        // Zusaetzlich (unabhaengig von modul_adress) wird dieselbe 1-Bit-
        // Auswertung an updateGroupFromAddress() gemeldet, damit sie auch
        // als eine der 2 Gruppen fuer "MKan" (80-95) zaehlen kann, falls die
        // Adresse dort konfiguriert ist.
        case MWset4:{
            uint8_t a=crsf.get_cmd_buffer(7);
            uint16_t s=((uint16_t)crsf.get_cmd_buffer(8)<<8)|crsf.get_cmd_buffer(9);
            uint8_t bits=compressSwitches(s);
            if(a==(uint8_t)config.modul_adress) einkanal_Data=bits;
            updateGroupFromAddress(a,bits);
            break;
        }
        case MWset4m:{
            uint8_t c=min((uint8_t)crsf.get_cmd_buffer(7),(uint8_t)7);
            for(uint8_t i=0;i<c;i++){
                uint8_t a=crsf.get_cmd_buffer(8+(3*i));
                uint16_t s=((uint16_t)crsf.get_cmd_buffer(9+(3*i))<<8)|crsf.get_cmd_buffer(10+(3*i));
                uint8_t bits=compressSwitches(s);
                if(a==(uint8_t)config.modul_adress) einkanal_Data=bits;
                updateGroupFromAddress(a,bits);
            }
            break;
        }
        // MWset liefert bereits 1 Bit/Schalter direkt (kein Kompaktieren noetig).
        case MWset:{
            uint8_t a=crsf.get_cmd_buffer(7);
            uint8_t b=crsf.get_cmd_buffer(8);
            if(a==(uint8_t)config.modul_adress) einkanal_Data=b;
            updateGroupFromAddress(a,b);
            break;
        }
        case MWprop:break;
    }
}
uint8_t compressSwitches(uint16_t s){uint8_t r=0;for(uint8_t i=0;i<8;i++)r|=((s>>(i*2))&0x1)<<i;return r;}

