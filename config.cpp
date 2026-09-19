/*
 * config.cpp  -  ESP32-RC-Sound
 */

#include "config.h"
#include <Preferences.h>
#include <initializer_list>

ConfigData    config;
bool          configDirty   = false;
unsigned long configDirtyMs = 0;

static constexpr const char* NVS_NS = "rcsound";

// Siehe isValidEinSource() in ESP32-RC-Sound.ino (Definition dort, damit
// dieselbe Bereichspruefung wie einTyp() verwendet wird statt einer
// zweiten, potenziell abweichenden Kopie).
extern bool isValidEinSource(int c);

void markDirty() { configDirty = true; configDirtyMs = millis(); }

void Reset_all() {
    for (int i = 0; i < RCSOUND_NUM_SLOTS; i++) {
        config.Source_Start_Sound[i] = 999;
        config.Volumen_Sound[i]      = 100;
        config.Mode_Sound[i]         = 0;
    }
    config.Source_Speed_Sound_0   = 999;
    config.throttle_mode          = 0;
    config.Min_Speed_Sound_0      = 100;
    config.Max_Speed_Sound_0      = 300;
    config.shutdowndelay          = 0;
    config.engine_on_toggle       = 0;
    config.throttle_ramp          = 20;
    config.throttle_dead_band     = 10;
    config.Einkanal_Channel       = 999;
    config.Einkanal_mode          = 0;
    config.Einkanal_RC_System     = 0;
    config.modul_adress           = 0;
    // Gruppe 1 (Index 0) defaultmaessig = modul_adress (0) - Einkanal-/
    // Einzelbetrieb funktioniert "out of the box" auch ohne weitere
    // Konfiguration; die zweite Gruppe ist bewusst deaktiviert (255), bis
    // der Nutzer sie explizit einstellt (siehe README_V5).
    config.EK_Gruppen_Adresse[0]  = 0;
    for (int i = 1; i < RCSOUND_NUM_GROUPS; i++) config.EK_Gruppen_Adresse[i] = 255;
    // SBUS-Gruppen (siehe config.h) starten alle deaktiviert (999 = kein
    // Kanal zugewiesen) - anders als bei EK_Gruppen_Adresse gibt es hier
    // keine sinnvolle "out of the box"-Vorbelegung, da jede Gruppe erst
    // einen freien SBUS-Kanal braucht.
    for (int i = 0; i < RCSOUND_NUM_GROUPS; i++) { config.SBUS_Gruppen_Channel[i] = 999; config.SBUS_Gruppen_Mode[i] = 0; }
    config.sport_poll_id[0]       = 0xA1;   // Physical ID 0x02 (Werkseinstellung Sensor 1)
    config.sport_poll_id[1]       = 0x22;   // Physical ID 0x03 (Sensor 2 umprogrammiert)
    // Port B und Port C starten bewusst deaktiviert (siehe
    // Migrationshinweis in config.h) - PortB_Baud bekommt trotzdem einen
    // sinnvollen Platzhalter, damit ein spaeteres Umschalten auf CRSF ohne
    // vorherige Baudraten-Eingabe funktioniert.
    config.PortB_Mode             = PORTB_OFF;
    config.PortB_Baud             = PORTB_BAUD_DEFAULT_CRSF;
    config.PortC_GPS_Enabled      = 0;
    config.Source_Ebenen_Um_Kanal = 999;
    config.Source_Ebenen_Kanal    = 999;
    config.Hardware_Config        = 0;
    config.Hardware_Config_Pending = 0;
    config.PWM_scale_min          = 1000;
    config.PWM_scale_max          = 2000;
    strncpy(config.WiFi_SSID,     "ESP32-RC-Sound", sizeof(config.WiFi_SSID)     - 1);
    strncpy(config.WiFi_Password, "123456789",      sizeof(config.WiFi_Password) - 1);
    strncpy(config.WiFi_IP,       "192.168.1.1",    sizeof(config.WiFi_IP)       - 1);
    strncpy(config.Device_Name,   "RC-Sound",       sizeof(config.Device_Name)   - 1);
    config.WiFi_SSID[sizeof(config.WiFi_SSID)-1]         = '\0';
    config.WiFi_Password[sizeof(config.WiFi_Password)-1] = '\0';
    config.WiFi_IP[sizeof(config.WiFi_IP)-1]             = '\0';
    config.Device_Name[sizeof(config.Device_Name)-1]     = '\0';
    // Siehe Erklaerung in config.h - bewusst per Default AUS.
    config.WifiAutoEnable      = false;
    config.WifiAutoTimeoutSec  = 60;
    configDirty = true;
}

static void applyPreset(const int srcs[RCSOUND_NUM_SLOTS], int spd) {
    for (int i = 0; i < RCSOUND_NUM_SLOTS; i++) { config.Source_Start_Sound[i]=srcs[i]; config.Volumen_Sound[i]=100; config.Mode_Sound[i]=0; }
    config.Source_Speed_Sound_0=spd; config.Min_Speed_Sound_0=100; config.Max_Speed_Sound_0=300;
    config.shutdowndelay=0; config.engine_on_toggle=0; config.Einkanal_Channel=999;
    config.Source_Ebenen_Um_Kanal=999; config.Source_Ebenen_Kanal=999;
    config.throttle_ramp=20; config.throttle_dead_band=10;
    config.PWM_scale_min=1000; config.PWM_scale_max=2000;
    configDirty=true;
}
// Presets belegen nur die ersten Slots, der Rest bleibt 999 (deaktiviert) -
// fillPreset() vermeidet, alle RCSOUND_NUM_SLOTS Werte von Hand
// auszuschreiben.
static void fillPreset(int (&out)[RCSOUND_NUM_SLOTS], std::initializer_list<int> head) {
    int i = 0;
    for (int v : head) out[i++] = v;
    for (; i < RCSOUND_NUM_SLOTS; i++) out[i] = 999;
}
void set_sbus() { int s[RCSOUND_NUM_SLOTS]; fillPreset(s,{10,11,12,13,14}); applyPreset(s,1);  }
void set_pwm()  { int s[RCSOUND_NUM_SLOTS]; fillPreset(s,{41,42,43,44,45}); applyPreset(s,20); }
void set_pin()  { int s[RCSOUND_NUM_SLOTS]; fillPreset(s,{61,62,63,64});    applyPreset(s,999); }

void loadConfig() {
    Reset_all(); configDirty=false;
    Preferences p; p.begin(NVS_NS,true);
    // Siehe ausfuehrliche Begruendung bei saveConfigForce() weiter unten -
    // die drei Sound-Arrays werden als EIN Blob pro Array (3 Keys) abgelegt.
    // "sssArr" ist der Blob-Key; falls er fehlt, aber der aeltere Einzel-Key
    // "sss0" existiert, werden die alten Keys einmalig eingelesen und beim
    // naechsten Speichern automatisch ins Blob-Format migriert
    // (configDirty=true weiter unten).
    bool hasBlob = p.isKey("sssArr");
    bool hasOld  = p.isKey("sss0");
    if (!hasBlob && !hasOld) { p.end(); configDirty=true; return; }
    // needsMigrationSave: haelt fest, ob waehrend dieser Funktion etwas
    // geladen wurde, das noch nicht im aktuellen NVS-Format gespeichert ist
    // (altes Sound-Array-Format hier, oder ein uebernommener Hardware_Config-
    // Wunsch weiter unten), damit saveConfig() (das configDirty prueft) ein
    // solches Zwischenergebnis automatisch wegschreibt, statt es zu
    // verwerfen.
    bool needsMigrationSave = false;
    char key[8];
    // Faengt einen Sonderfall ab: der Blob "sssArr" EXISTIERT (Geraet ist
    // laengst auf dem Blob-Format), hat aber eine ANDERE Groesse als aktuell
    // erwartet, weil sich RCSOUND_NUM_SLOTS zwischenzeitlich geaendert hat.
    // Ohne diese Pruefung wuerde das ALLE Slots stillschweigend auf die
    // Defaults (999/100/0) zuruecksetzen UND das beim naechsten Speichern
    // dauerhaft machen - die eigentlich vorhandene Konfiguration waere ohne
    // jede Fehlermeldung verloren gegangen. Ist der Blob vorhanden und seine
    // Laenge ein "glattes" Vielfaches von sizeof(int) (also ein Blob aus
    // genau diesem Format, nur mit anderer Slot-Anzahl), werden die
    // vorhandenen Werte fuer die gemeinsame Anzahl Slots UEBERNOMMEN und nur
    // ueberzaehlige (beim Verkleinern) bzw. fehlende (beim Vergroessern)
    // Slots auf Default gesetzt - funktioniert in beide Richtungen.
    size_t sssLen = hasBlob ? p.getBytesLength("sssArr") : 0;
    size_t vsLen  = hasBlob ? p.getBytesLength("vsArr")  : 0;
    size_t msLen  = hasBlob ? p.getBytesLength("msArr")  : 0;
    if (hasBlob
        && sssLen == sizeof(config.Source_Start_Sound)
        && vsLen  == sizeof(config.Volumen_Sound)
        && msLen  == sizeof(config.Mode_Sound))
    {
        p.getBytes("sssArr", config.Source_Start_Sound, sizeof(config.Source_Start_Sound));
        p.getBytes("vsArr",  config.Volumen_Sound,       sizeof(config.Volumen_Sound));
        p.getBytes("msArr",  config.Mode_Sound,          sizeof(config.Mode_Sound));
    } else if (hasBlob && sssLen > 0 && sssLen == vsLen && sssLen == msLen && (sssLen % sizeof(int)) == 0) {
        size_t oldCount = sssLen / sizeof(int);
        size_t copyCount = oldCount < (size_t)RCSOUND_NUM_SLOTS ? oldCount : (size_t)RCSOUND_NUM_SLOTS;
        p.getBytes("sssArr", config.Source_Start_Sound, copyCount*sizeof(int));
        p.getBytes("vsArr",  config.Volumen_Sound,       copyCount*sizeof(int));
        p.getBytes("msArr",  config.Mode_Sound,          copyCount*sizeof(int));
        for (size_t i = copyCount; i < (size_t)RCSOUND_NUM_SLOTS; i++) {
            config.Source_Start_Sound[i] = 999;
            config.Volumen_Sound[i]      = 100;
            config.Mode_Sound[i]         = 0;
        }
        Serial.printf("Sound-Slots: NVS-Blob hatte %u statt %d Slots (RCSOUND_NUM_SLOTS geaendert) - vorhandene Werte uebernommen, Rest auf Default.\n",
                      (unsigned)oldCount, RCSOUND_NUM_SLOTS);
        needsMigrationSave = true;
    } else {
        // Aelteres Format (echte Einzel-Keys statt Blob) - Einzel-Keys
        // lesen, Defaults (999/100/0) fuer fehlende Slots. Naechstes
        // Speichern migriert.
        for(int i=0;i<RCSOUND_NUM_SLOTS;i++){
            snprintf(key,sizeof(key),"sss%d",i); config.Source_Start_Sound[i]=p.getInt(key,999);
            snprintf(key,sizeof(key),"vs%d",i);  config.Volumen_Sound[i]=p.getInt(key,100);
            snprintf(key,sizeof(key),"ms%d",i);  config.Mode_Sound[i]=p.getInt(key,0);
        }
        needsMigrationSave = true;
    }
    config.Source_Speed_Sound_0   = p.getInt("spd",  999);
    config.throttle_mode          = p.getInt("thrm", 0);
    config.Min_Speed_Sound_0      = p.getInt("minsp",100);
    config.Max_Speed_Sound_0      = p.getInt("maxsp",300);
    config.shutdowndelay          = p.getInt("shdly",0);
    config.engine_on_toggle       = p.getInt("entog",0);
    config.throttle_ramp          = p.getInt("thrr", 20);
    config.throttle_dead_band     = p.getInt("thrdb",10);
    config.Einkanal_Channel       = p.getInt("ekch", 999);
    config.Einkanal_mode          = p.getInt("ekmo", 0);
    config.Einkanal_RC_System     = p.getInt("ekrc", 0);
    config.modul_adress           = p.getInt("madr", 0);
    for (int i = 0; i < RCSOUND_NUM_GROUPS; i++) {
        snprintf(key,sizeof(key),"gadr%d",i);
        config.EK_Gruppen_Adresse[i] = p.getInt(key, (i==0) ? 0 : 255);
    }
    for (int i = 0; i < RCSOUND_NUM_GROUPS; i++) {
        snprintf(key,sizeof(key),"sgch%d",i);
        int ch = p.getInt(key, 999);
        config.SBUS_Gruppen_Channel[i] = (ch==999) ? 999 : constrain(ch,0,15);
        snprintf(key,sizeof(key),"sgmo%d",i);
        // Kein constrain(0,3) - gueltige Werte sind 0 und 10-13 (siehe
        // SBUS_Gruppen_Mode in config.h), genau wie bei Einkanal_mode oben.
        config.SBUS_Gruppen_Mode[i] = p.getInt(key, 0);
    }
    config.sport_poll_id[0]       = (uint8_t)p.getInt("spid0", 0xA1);
    config.sport_poll_id[1]       = (uint8_t)p.getInt("spid1", 0x22);
    config.Source_Ebenen_Um_Kanal = p.getInt("ebum", 999);
    config.Source_Ebenen_Kanal    = p.getInt("ebk",  999);
    config.Hardware_Config        = p.getInt("hwcfg",0);
    // Hardware_Config kennt nur V1/V2/V3 (0-2). Ein aelterer NVS-Stand mit
    // 3 oder 4 wird hier auf 2 (V3) begrenzt - die Platine/Pinbelegung war
    // identisch, nur S.Port/GPS liefen dort automatisch mit. Bewusst KEINE
    // automatische Uebernahme von PortB_Mode/PortC_GPS_Enabled (siehe
    // config.h) - diese starten auf "Aus" und muessen neu gewaehlt werden.
    if (config.Hardware_Config > 2) {
        Serial.printf("Hardware_Config: altes V%d (v5.x) -> V3 begrenzt (v6.0 Port-Modell, Port B/C stehen auf Aus)\n",
                      config.Hardware_Config + 1);
        config.Hardware_Config = 2;
        needsMigrationSave = true;
    }
    // Wunsch-Wert laden (Default = aktueller Wert, falls noch kein eigener
    // Wunsch-Wert im NVS steht -> kein ungewollter Wechsel beim Boot). Erst
    // HIER, beim Booten, wird ein zwischenzeitlich per Web/Lua gesetzter
    // Wunsch tatsaechlich wirksam - alle nachfolgenden setup()-Schritte
    // (S.Port/GPS/Pins) sehen dann konsistent den neuen Wert.
    config.Hardware_Config_Pending = p.getInt("hwcfgp", config.Hardware_Config);
    if (config.Hardware_Config_Pending > 2) config.Hardware_Config_Pending = 2;
    if (config.Hardware_Config_Pending != config.Hardware_Config) {
        Serial.printf("Hardware_Config: %d -> %d (Wunsch aus letzter Sitzung uebernommen)\n",
                      config.Hardware_Config, config.Hardware_Config_Pending);
        config.Hardware_Config = config.Hardware_Config_Pending;
        needsMigrationSave = true;
    }
    config.PortB_Mode             = p.getInt("pbmode", PORTB_OFF);
    if (config.PortB_Mode < PORTB_OFF || config.PortB_Mode > PORTB_RAW) config.PortB_Mode = PORTB_OFF;
    config.PortB_Baud             = (uint32_t)p.getInt("pbbaud", (int)PORTB_BAUD_DEFAULT_CRSF);
    config.PortC_GPS_Enabled      = p.getInt("pcgps", 0) ? 1 : 0;
    config.PWM_scale_min          = p.getInt("pwmin",1000);
    config.PWM_scale_max          = p.getInt("pwmax",2000);
    String ssid=p.getString("ssid","ESP32-RC-Sound");
    String pass=p.getString("pass","123456789");
    String ip  =p.getString("wip", "192.168.1.1");
    String dnam=p.getString("dnam","RC-Sound");
    // WLAN-Failsafe-Einstellungen (siehe config.h) - beide als "int"
    // gespeichert, wie auch andere einfache Flags/Zahlen in dieser Funktion
    // (z.B. "pcgps"), kein eigener Bool-/UShort-Preferences-Zugriff noetig.
    // MUSS vor p.end() gelesen werden (wie alle anderen p.get*() oben).
    bool     wifiAutoEnable  = p.getInt("wfauto", 0) ? true : false;
    uint16_t wifiAutoTimeout = (uint16_t)constrain(p.getInt("wftmo", 60), 5, 240);
    p.end();
    strncpy(config.WiFi_SSID,    ssid.c_str(),sizeof(config.WiFi_SSID)-1);
    strncpy(config.WiFi_Password,pass.c_str(),sizeof(config.WiFi_Password)-1);
    strncpy(config.WiFi_IP,      ip.c_str(),  sizeof(config.WiFi_IP)-1);
    strncpy(config.Device_Name,  dnam.c_str(),sizeof(config.Device_Name)-1);
    config.WiFi_SSID[sizeof(config.WiFi_SSID)-1]='\0';
    config.WiFi_Password[sizeof(config.WiFi_Password)-1]='\0';
    config.WiFi_IP[sizeof(config.WiFi_IP)-1]='\0';
    config.Device_Name[sizeof(config.Device_Name)-1]='\0';
    config.WifiAutoEnable     = wifiAutoEnable;
    config.WifiAutoTimeoutSec = wifiAutoTimeout;
    // Siehe ausfuehrliche Erklaerung bei isValidEinSource() in
    // ESP32-RC-Sound.ino - ein ungueltiger Source_Start_Sound[i]-Rohwert
    // (z.B. aus einer inzwischen entfernten Gruppe der Mehrfachadress-
    // Erweiterung, Rohwerte 96-103) wird beim Laden erkannt, klar geloggt
    // und auf 999 (Deaktiviert) bereinigt, statt unveraendert und
    // unsichtbar in der NVS/in jedem Export zu verbleiben.
    for (int i = 0; i < RCSOUND_NUM_SLOTS; i++) {
        if (!isValidEinSource(config.Source_Start_Sound[i])) {
            Serial.printf("Sound %d: ungueltiger/veralteter Quellwert %d (vermutlich alte Gruppe 3 der "
                          "Mehrfachadress-Erweiterung, entfernt in v6.07) -> auf Deaktiviert (999) zurueckgesetzt.\n",
                          i, config.Source_Start_Sound[i]);
            config.Source_Start_Sound[i] = 999;
            needsMigrationSave = true;
        }
    }
    // configDirty spiegelt hier bewusst needsMigrationSave: nur so haben
    // die Sound-Array-Migration oben und der Hardware_Config_Pending-
    // Uebernahmefall (weiter oben) ueber saveConfig() (das configDirty
    // prueft) eine Chance, automatisch weggeschrieben zu werden -
    // saveConfigForce() laeuft zwar bei jedem Boot ohnehin einmal, aber
    // "configDirty" soll fuer diese beiden Faelle nicht wirkungslos sein.
    configDirty = needsMigrationSave;
    Serial.println("Config aus NVS geladen.");
}

void saveConfig() { if(!configDirty)return; saveConfigForce(); }

void saveConfigForce() {
    Preferences p; p.begin(NVS_NS,false);
    char key[8];
    // Die 3 Sound-Arrays werden als 3 Blob-Writes (ein put/getBytes je
    // Array) geschrieben statt als RCSOUND_NUM_SLOTS (25) einzelne
    // p.putInt()-Aufrufe pro Array. Jeder einzelne Preferences-put*-Aufruf
    // committet fuer sich auf den externen Flash (kein Batching) - viele
    // synchrone Flash-Commits direkt im HTTP-Handler von /api/config
    // wuerden spuerbar lange blockieren (Groessenordnung Zehntel-Sekunden),
    // waehrend loop()/server.handleClient() (und damit auch die
    // WLAN-Stack-Bedienung) nicht laeuft - siehe passende Lade-Logik samt
    // Migration alter Einzel-Keys in loadConfig().
    p.putBytes("sssArr", config.Source_Start_Sound, sizeof(config.Source_Start_Sound));
    p.putBytes("vsArr",  config.Volumen_Sound,       sizeof(config.Volumen_Sound));
    p.putBytes("msArr",  config.Mode_Sound,          sizeof(config.Mode_Sound));
    p.putInt("spd",  config.Source_Speed_Sound_0);
    p.putInt("thrm", config.throttle_mode);
    p.putInt("minsp",config.Min_Speed_Sound_0);
    p.putInt("maxsp",config.Max_Speed_Sound_0);
    p.putInt("shdly",config.shutdowndelay);
    p.putInt("entog",config.engine_on_toggle);
    p.putInt("thrr", config.throttle_ramp);
    p.putInt("thrdb",config.throttle_dead_band);
    p.putInt("ekch", config.Einkanal_Channel);
    p.putInt("ekmo", config.Einkanal_mode);
    p.putInt("ekrc", config.Einkanal_RC_System);
    p.putInt("madr", config.modul_adress);
    for (int i = 0; i < RCSOUND_NUM_GROUPS; i++) {
        snprintf(key,sizeof(key),"gadr%d",i);
        p.putInt(key, config.EK_Gruppen_Adresse[i]);
    }
    for (int i = 0; i < RCSOUND_NUM_GROUPS; i++) {
        snprintf(key,sizeof(key),"sgch%d",i);
        p.putInt(key, config.SBUS_Gruppen_Channel[i]);
        snprintf(key,sizeof(key),"sgmo%d",i);
        p.putInt(key, config.SBUS_Gruppen_Mode[i]);
    }
    p.putInt("spid0",(int)config.sport_poll_id[0]);
    p.putInt("spid1",(int)config.sport_poll_id[1]);
    p.putInt("ebum", config.Source_Ebenen_Um_Kanal);
    p.putInt("ebk",  config.Source_Ebenen_Kanal);
    p.putInt("hwcfg",config.Hardware_Config);
    p.putInt("hwcfgp",config.Hardware_Config_Pending);
    p.putInt("pbmode",config.PortB_Mode);
    p.putInt("pbbaud",(int)config.PortB_Baud);
    p.putInt("pcgps", config.PortC_GPS_Enabled);
    p.putInt("pwmin",config.PWM_scale_min);
    p.putInt("pwmax",config.PWM_scale_max);
    p.putString("ssid",config.WiFi_SSID);
    p.putString("pass",config.WiFi_Password);
    p.putString("wip", config.WiFi_IP);
    p.putString("dnam",config.Device_Name);
    // Siehe config.h/loadConfig() - der manuelle "WLAN jetzt"-Schalter
    // (Web/Lua-Feld 174) wird bewusst NICHT hier mit gespeichert, nur die
    // Failsafe-Einstellungen.
    p.putInt("wfauto", config.WifiAutoEnable ? 1 : 0);
    p.putInt("wftmo",  (int)config.WifiAutoTimeoutSec);
    p.end();
    configDirty=false;
    Serial.println("Config in NVS gespeichert.");
}

// Siehe Erklaerung in config.h. Eigener, kleiner Preferences-Zugriff (nicht
// Teil von saveConfigForce()/loadConfig(), da unabhaengig von der
// eigentlichen Geraetekonfiguration) - ein einzelner Int-Key, kein Blob.
uint32_t nextSdBackupCounter() {
    Preferences p; p.begin(NVS_NS,false);
    uint32_t n = p.getUInt("sdbkupn", 0) + 1;
    p.putUInt("sdbkupn", n);
    p.end();
    return n;
}
