# ESP32-RC-Sound V7

Soundmodul-Firmware für RC-Modelle auf ESP32-Basis. Spielt WAV-Dateien von einer SD-Karte über I2S ab, gesteuert über den RC-Bus (SBUS oder CRSF/ELRS), und lässt sich vollständig über eine eingebaute Weboberfläche oder direkt am Sender im CRSF-/Lua-Parametermenü konfigurieren.

Für Fahrzeuge, Baumaschinen, Panzer, Boote — überall dort, wo ein Modell klingen soll wie das Original.

---

## Funktionen

- **Motorsound** mit Start-/Loop-/Stopp-Zyklus, Gasrampe und einstellbarer Standgas-Verzögerung; die Loop-Tonhöhe folgt dem Gaskanal
- **24 frei zuweisbare Zusatzsounds** (Hupe, Blinker, Licht, Sirene, Ladeklappe …), je Slot mit eigener Quelle, Lautstärke und Wiedergabemodus (Normal / Loop / Tippbetrieb)
- **RC-Systeme:** FrSky, FlySky, ELRS (SBUS), Hott, ELRS (CRSF)
- **Mehrfachadress-Erweiterung („MKan")** — 16 zusätzliche Einzelkanäle über zwei Gruppen, wahlweise per CRSF-Gruppenadresse oder per SBUS-Gruppenkanal
- **Telemetrie:** S.Port-LiPo-Zellüberwachung (FLVSS/MLVSS, 1–6S automatisch erkannt) und GPS-Geschwindigkeit
- **Weboberfläche** über eigenen Access Point: Konfiguration, Sound-Upload/-Löschen, Konfigurations-Backup als JSON oder auf SD-Karte
- **CRSF-/Lua-Parametermenü** — vollständige Bedienung am Sender ohne Handy oder PC (Feld-IDs 1–180)
- **OTA-Update** per WLAN, ohne Ausbau des Moduls
- **WLAN-Failsafe:** schaltet den AP automatisch ein, wenn längere Zeit kein gültiges RC-Signal anliegt

---

## Hardware

Getestet auf gängigen ESP32-DevKit-Boards (PlatformIO-Board `esp32dev`), mit SD-Kartenmodul und I2S-Verstärker (z. B. MAX98357A).

### Pin-Belegung

| Funktion | GPIO |
|---|---|
| WLAN-Aktivierung (LOW beim Booten = AP-Modus) | 13 |
| CRSF RX / SBUS RX | 16 |
| CRSF TX | 17 |
| SD: CS / CLK / MISO / MOSI | 5 / 18 / 19 / 23 |
| I2S: DOUT / LRC / BCLK | 21 / 25 / 26 |
| Port B — RX / TX (nur V3) | 32 / 33 |
| Port C — GPS (SoftwareSerial) | 27 / 14 |

### Hardware-Varianten

| Variante | Eingänge |
|---|---|
| **V1** | BUS + PWM-Eingang + GPIO-Pin + Einkanal (GPIO 22, 0, 2, 4) |
| **V2** | wie V1, andere Pins (GPIO 14, 27, 32, 33) |
| **V3** | nur BUS-Kanal + Einkanal, dafür Port B und Port C |

### Port-Modell (V3)

- **Port A** — RC-Eingang, fest belegt (CRSF oder SBUS-Familie)
- **Port B** — zweiter Hardware-UART, Rolle frei wählbar: Aus, S.Port-Master, CRSF (zweite Instanz), Hott (Menü vorhanden, Treiber offen) oder Raw-UART, je mit eigener Baudrate
- **Port C** — GPS für CRSF-Telemetrie, unabhängig zuschaltbar

> **Achtung:** Port C (GPS) und die SBUS-Familie belegen beide GPIO 27. Bei gleichzeitiger Aktivierung bleibt GPS deaktiviert und die Firmware meldet einen GPIO27-Konflikt. Für GPS das RC-System auf ELRS (CRSF) stellen.

S.Port-Verdrahtung an Port B: `GPIO33 →|1N4148|→ S.Port-Signal ← GPIO32`

---

## Sounds auf der SD-Karte

Dateien liegen im Wurzelverzeichnis der Karte, die Namen sind fest vorgegeben:

```
loop.wav  shut.wav  start.wav          Motorsound
sound1.wav … sound24.wav               Zusatzsounds 1–24
```

**Format:** 16-Bit-PCM-WAV, mono oder stereo, maximal 44.100 Hz.

Dateien lassen sich per Kartenleser kopieren oder direkt über die Weboberfläche hochladen und löschen — eine hochgeladene Datei wirkt sofort, ohne Neustart.

---

## Bauen und Flashen

### PlatformIO (empfohlen)

```bash
pio run -e esp32dev              # Firmware bauen
pio run -e esp32dev -t upload    # bauen und flashen
pio test -e native               # Host-Tests, ohne ESP32-Hardware
```

Bibliotheksversionen sind in `platformio.ini` fest gepinnt. Die Partitionstabelle steht auf `default_ota.csv` — zwei App-Partitionen, damit OTA funktioniert.

### Arduino IDE

Benötigte Bibliotheken:

- Bolder Flight Systems SBUS 8.1.4
- [CRSF_ESP32](https://github.com/Ziege-One/CRSF_ESP32) — nicht in den Registries, als lokale Kopie einbinden
- Nur für GPS: `EspSoftwareSerial`, `TinyGPSPlus`

Im Menü **Werkzeuge → Partition Scheme** ein OTA-fähiges Schema wählen, sonst funktioniert das Firmware-Update per WLAN nicht. Der Ordner `lib/` und `test/` gehören **nicht** in den Sketch-Ordner — nur der Inhalt von `src/`, sonst meldet der Compiler „multiple definition of …".

### OTA-Update

Über die Karte „Firmware-Update (OTA)" im Tab *Einstellungen* wird eine kompilierte `.bin` per WLAN in das inaktive OTA-Segment geschrieben. Schlägt das Update fehl oder wird es abgebrochen, bleibt die bisherige Firmware aktiv und startfähig.

> **Praxistipp:** OTA möglichst mit über ESC/Akku bestromtem Modul durchführen. Manche USB-Anschlüsse liefern beim Flash-Schreiben nicht genug Strom; ein Brownout-Reset bricht den Upload ab.

---

## Konfiguration

### WLAN aktivieren

Drei Wege zum Access Point:

1. **GPIO 13 beim Einschalten kurz auf GND** — funktioniert immer, auch ganz ohne Sender
2. **Schalter im Web-Tab „WiFi"** bzw. CRSF-/Lua-Feld 174 — wirkt sofort, ohne Neustart
3. **Auto-Failsafe** — schaltet selbsttätig ein, wenn 5–240 s (Standard 60 s) kein gültiges RC-Signal anliegt

Der Failsafe schaltet **nur ein, nie automatisch wieder aus**. Das ist Absicht: sonst bräche eine laufende Konfiguration oder ein laufendes OTA-Update ab, sobald kurz wieder ein Signal hereinkommt.

### Am Sender

Bei RC-System ELRS (CRSF) ist das Modul vollständig über das native Parametermenü des Senders bedienbar, etwa als Lua-Script in EdgeTX. Jedes Modul bezieht seine CRSF-Busadresse (`0xC0` + WM-Adresse) und seinen Telemetrie-Zeitslot aus der WM-Adresse nach dem Wilhelm-Meier-Schema — mehrere Module am selben Bus stören sich dadurch nicht.

---

## Fehlerbehebung

| Symptom | Ursache | Lösung |
|---|---|---|
| Sound-Upload meldet `sd_locked`, Löschen geht auch nicht | Die WAV-Datei auf der Karte trägt das FAT-Attribut *schreibgeschützt*. Windows übernimmt es beim Kopieren oft unbemerkt — und nur für einzelne Dateien | Am PC prüfen mit `attrib X:\*.wav`, aufheben mit `attrib -r X:\*.wav` |
| Upload meldet `sd_write` | Karte voll, schreibgeschützt oder defekt | Serial-Log ansehen: die Firmware gibt bei jedem Fehlschlag freien Kartenspeicher, freigegebene Datei-Handles und freien Heap aus |
| Firmware startet, aber kein AP sichtbar | GPIO 13 beim Start nicht auf LOW | Taster/Brücke gegen GND beim Einschalten, oder Failsafe abwarten |
| GPS meldet „GPIO27-Konflikt" | Port C und SBUS gleichzeitig aktiv | RC-System auf ELRS (CRSF) stellen oder GPS deaktivieren |
| Port B bleibt inaktiv, „kein freier Hardware-UART" | Port B würde denselben UART wie Port A belegen | RC-System und Port-B-Rolle prüfen — die Kollisionsprüfung verhindert das bewusst |
| „multiple definition of …" beim Kompilieren | `lib/` und `test/` wurden in den Sketch-Ordner kopiert | Nur den Inhalt von `src/` verwenden |

### Hinweis für Mitentwickler: `max_files` in `SD.begin()`

Der Standardwert (5 gleichzeitig offene Dateien) sieht knapp aus und lädt zum Anheben ein. **Nicht tun.** Ein höherer Wert belegt beim Mounten dauerhaft einen deutlich größeren Block im internen DRAM, und der verhindert, dass der WLAN-Treiber seine *zusammenhängenden* Puffer bekommt:

```
E wifi: Expected to init 4 rx buffer, actual is 3
E wifi: rc_enable_trc fail, no mem
```

Das tritt auf, obwohl über 100 KB Heap frei sind — es ist ein Fragmentierungs-, kein Platzproblem. Der Engpass an Datei-Handles wird stattdessen gelöst, indem während eines Uploads nicht benötigte Handles freigegeben und danach neu geladen werden (`releaseIdleSoundHandles()` in `WebServerManager.cpp`).

---

## Projektstruktur

```
src/ESP32-RC-Sound/
  ESP32-RC-Sound.ino     Hauptprogramm, Sound-Logik, CRSF-Parametermenü
  config.cpp/.h          Persistente Konfiguration (NVS), Migration
  WebServerManager.cpp   Weboberfläche, REST-API, OTA
  XT_I2S_Audio.cpp/.h    WAV-Wiedergabe über I2S, SD-Zugriff
  crsf_esp32.cpp/.h      CRSF-Protokoll, Telemetrie, Discovery
  sport_lipo.cpp/.h      S.Port-Telemetrie (Port B)
  gps_speed.cpp/.h       GPS-Auswertung (Port C)
  port_function.cpp/.h   Port-B-Rollen-Registry
lib/port_registry/       Hardwareunabhängiger Spiegel der Registry
test/                    Unity-Tests, laufen ohne ESP32
```

`lib/port_registry/` spiegelt die Port-B-Logik hardwareunabhängig, damit sie per `pio test -e native` ohne Board geprüft werden kann. **Beide Seiten müssen bei Änderungen von Hand synchron gehalten werden.**

CI baut bei jedem Push die Firmware und führt die Host-Tests aus.

---

## Versionsstand

Aktuell: **v7.17**

Wichtige Schritte der V7-Linie:

| Version | Änderung |
|---|---|
| 7.00 | Eigenständiges Projekt (Fork von v6.12); WLAN manuell schaltbar + Auto-Failsafe |
| 7.04 / 7.05 | MKan auch bei SBUS-Betrieb, über zwei SBUS-Gruppenkanäle |
| 7.06 | Sound-Upload abgesichert: Datei wird vor dem Ersetzen geschlossen und danach neu geladen |
| 7.07 – 7.10 | **Zurückgezogen** — `max_files`-Anhebung zerstörte den WLAN-Start (siehe oben) |
| 7.11 – 7.13 | Rücknahme, Handle-Freigabe statt Reservierung, Upload-Diagnose, Klartext-Fehlermeldungen |
| 7.14 | Neu: "WLAN dauerhaft an" (`config.WifiAlwaysOn`, Default AN, CRSF/Lua-Feld 181) |
| 7.15 | Kein sofortiger `enableAP()`-Aufruf mehr aus Web-/CRSF-Handlern (Risikominimierung) |
| 7.16 | SD-Aktivitäts-Cooldown vor NVS-Schreibvorgängen (Feldbefund: Brownout-Reset-Loop bei ungepuffertem Modul ohne Stützkondensator direkt nach SD-Schreibzugriff); alle Config-Speicherpfade vereinheitlicht über den debounced Schreibvorgang in `loop()` |
| 7.17 | Fix: Lautstärkeänderung während einer laufenden Web-Testwiedergabe (Sound-Slots 1–24) wirkte bisher erst beim nächsten Trigger, nicht auf die bereits laufende Wiedergabe (`handleSound()`) |

Die vollständige Historie ab v5.1 steht in der Projektbeschreibung (`ESP32-RC-Sound-V7-Projektbeschreibung-*.docx`).

`firmware-v5`, `firmware-v6` und `firmware-v7` sind drei getrennte, unabhängig baubare Projekte ohne gemeinsame Versionszählung.

---

## Herkunft und Dank

Technische Basis ist **Frank Verfürths** Projekt *ESP32-SBUS-Switch* (2022) — von dort stammen der grundlegende Ansatz für SBUS-/CRSF-/S.Port-Protokoll, die WLAN-Weboberfläche und die I2S-Audioausgabe. Das Soundmodul ist daraus als eigenständiges, neu entworfenes Projekt hervorgegangen.

Die Audio-Engine (`XT_I2S_Audio`) stammt von **XTronical** und steht unter GNU GPL 3.0.

Das Adressierungsschema für mehrere Module am selben CRSF-Bus folgt dem Schema von **Wilhelm Meier**.

---

## Lizenz

Dieses Projekt enthält die unter **GNU GPL 3.0** stehende Audio-Engine von XTronical. Eine Lizenzdatei für das Gesamtprojekt steht noch aus — bis dahin gilt für den enthaltenen XTronical-Code dessen ursprüngliche Lizenz.
