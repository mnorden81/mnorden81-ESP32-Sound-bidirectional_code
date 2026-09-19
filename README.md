# ESP32-MultiSwitch v2.00

RC-gesteuerter 8-Kanal-Schalter für ESP32 mit Web-Interface.

*v2.00 erstellt von: PiperPilot*

## Änderungen v2.00 (übertragen aus dem Soundmodul-Projekt, ESP32-RC-Sound v7)

Diese Version übernimmt mehrere seit v1.42 im Soundmodul-Projekt gesammelte CRSF-Robustheits-Fixes sowie zwei neue, rein web-/WLAN-basierte Funktionen (kein SD-Karten-Slot vorhanden/nötig):

- **CRSF-Diagnose-Zähler** (rohe Bytes, gültige Frames, CRC-Fehler, empfangene DEVICE_PING/PARAMETER_READ/PARAMETER_WRITE) in `crsf_esp32.h`/`.cpp`, sichtbar im Debug-Tab der Weboberfläche – macht "geht nicht" ohne Seriell-Monitor sichtbar
- **BUS_OK-Fix**: bei CRSF jetzt an den Zähler für gültige Frames gekoppelt statt an `channel_output[0] > 0` – ein Kanalwert von 0 ist legitim und daher kein zuverlässiger Nachweis für einen frischen Frame
- **CRC-Fehler-Throttling**: Fehler werden gezählt statt bei jedem einzelnen sofort per `Serial.println()` ausgegeben – verhindert, dass anhaltendes Funkrauschen den Loop ausbremst; Sammel-Meldung höchstens alle 2 s
- **`updateDevice_Info()` abgesichert** gegen zu kurze/kaputte DEVICE_INFO-Frames (signed-Rechnung + Mindestlängen-Check statt möglichem Unsigned-Unterlauf)
- **Zieladressen-Prüfung bei `CRSF_FRAMETYPE_COMMAND`**, analog zu PING/PARAMETER_READ/WRITE (mit Broadcast-Ausnahme fürs WM-Protokoll, das weiterhin per Broadcast + Payload-Adresse filtert)
- **Konfiguration exportieren/importieren** als JSON-Datei über den Browser (WiFi-Tab → "Konfiguration sichern/wiederherstellen") – sichert alle 8 Ausgänge + globale Einstellungen, Import fragt vor dem Überschreiben (inkl. WLAN-Zugangsdaten) nach Bestätigung
- **WLAN-Auto-Failsafe** (WiFi-Tab, Default AUS): schaltet den Access Point automatisch ein, wenn über eine einstellbare Zeit (5–240 s) kein gültiges RC-Signal anliegt – z. B. wenn kein Sender gebunden ist. Schaltet nie automatisch wieder aus; ergänzend gibt es jetzt auch einen manuellen "WLAN jetzt ein/aus"-Schalter, der ohne Neustart sofort wirkt
- **Firmware-Update per WLAN (OTA)** (WiFi-Tab → "Firmware-Update (OTA)"): eine mit der Arduino-IDE gebaute `.bin`-Datei wird direkt im Browser hochgeladen und landet im gerade inaktiven OTA-Flash-Segment – Ausbauen und USB-Flashen ist für spätere Updates damit nicht mehr nötig. Schlägt der Upload fehl oder wird er abgebrochen, bootet das Modul unverändert mit der bisherigen Firmware weiter (kein Bricking-Risiko). **Voraussetzung:** einmalig ein OTA-fähiges Partitionsschema per USB flashen, siehe Abschnitt "Partitionsschema (Voraussetzung für OTA)" unten
- **Aufräumen**: `eeprom_esp32.h`, `hal_esp32.h`, `output_ctrl.h` sind weiterhin nicht in `MultiSwitch_ESP32.ino` eingebunden (Rest eines nicht gemergten Refactorings) – jetzt oben in den Dateien selbst klar als unbenutzt gekennzeichnet, damit das nicht zu Verwirrung führt

**Fix (nach erstem Test):** `webui_init()` rief `webServer.begin()` unabhängig vom AP-Status auf, ohne den WiFi-Treiber vorher zu initialisieren. Wenn der AP beim Booten nicht aktiviert wurde (GPIO 13 nicht LOW, Auto-Failsafe noch nicht ausgelöst), fehlten dessen interne FreeRTOS-Queues und es kam zum sofortigen Absturz (`assert failed: xQueueSemaphoreTake`) mit Bootloop. Behoben durch `WiFi.mode(WIFI_AP)` am Anfang von `webui_init()` – initialisiert nur den Treiber, der AP selbst bleibt bis zum ersten `webui_enableAP()`-Aufruf unsichtbar/inaktiv wie zuvor.

*Nicht übertragen:* Sound/GPS/S.Port/Port-B-Rollenmodell (Soundmodul-spezifisch, ohne Bezug zum Multiswitch) sowie das SD-Karten-Backup des Soundmoduls (das Multiswitch hat keinen SD-Slot – dafür Konfiguration exportieren/importieren, siehe oben).

### Partitionsschema (Voraussetzung für OTA)

Das OTA-Update schreibt die neue Firmware in ein zweites, aktuell inaktives App-Segment im Flash, während die laufende Firmware unverändert weiterläuft. Das Standard-Partitionsschema der Arduino-IDE reserviert dafür aber nur ein einziges, großes App-Segment – OTA ist damit erst nutzbar, nachdem **einmalig per USB** ein OTA-fähiges Partitionsschema geflasht wurde:

1. Arduino-IDE → **Werkzeuge → Partition Scheme**
2. Ein Schema mit "OTA" im Namen wählen, z. B. **"Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)"**
3. Einmal ganz normal per USB hochladen (Sketch → Hochladen)

Danach stehen zwei App-Partitionen (`ota_0`/`ota_1`) mit je ca. 1,9 MB zur Verfügung – reichlich Platz, da das Multiswitch (anders als das Soundmodul mit SD/Sound/SPIFFS-Bedarf, dort nur ca. 1,25 MB je Partition) keine SPIFFS-Daten benötigt. Ab diesem Zeitpunkt funktionieren weitere Updates bequem per WLAN über die Weboberfläche – ein erneuter USB-Vorgang ist dafür nicht mehr nötig, außer das Partitionsschema soll erneut geändert werden.

Ohne diesen einmaligen Schritt schlägt der OTA-Upload mit einem Fehler fehl (`Update.begin()` liefert dann `false`, da kein zweites App-Segment existiert) – das Modul bleibt dabei unverändert mit der bisherigen Firmware lauffähig.

*Hinweis:* Bei manchen Boards enthält bereits das Standard-Partitionsschema für 4-MB-Module zwei App-Partitionen. Am einfachsten also einfach einen OTA-Versuch starten – schlägt er mit dem Fehler `update_begin` fehl, ist das das eindeutige Signal, dass das Partitionsschema wie oben beschrieben umgestellt werden muss.

### Praxis-Hinweise für das OTA-Update (Erfahrungen aus dem Soundmodul-Projekt)

- **`.bin`-Datei erzeugen:** Arduino-IDE → Sketch → **Kompilierte Programmdatei exportieren** (Strg+Alt+S). Das kompiliert wie beim normalen Hochladen, schreibt das Ergebnis aber zusätzlich als Datei in einen `build/...`-Unterordner des Sketch-Ordners, statt es nur in einem temporären Cache abzulegen.
- **Richtige Datei:** benötigt wird ausschließlich `MultiSwitch_ESP32.ino.bin` – die Dateien `...bootloader.bin` und `...partitions.bin` im selben Ordner werden nicht gebraucht, die betreffen nur das allererste USB-Flashen eines neuen Moduls.
- **Speicherbedarf prüfen:** Am Ende des Kompiliervorgangs zeigt die IDE „Der Sketch verwendet ... Bytes (NN %) des Programmspeicherplatzes". Solange der Wert unter 100 % liegt, passt die Firmware in eine OTA-Partition.
- ⚠️ **Stromversorgung während des Updates:** Bestätigte Fehlerursache aus dem Soundmodul-Projekt – bei Versorgung nur über USB kann der Spannungseinbruch beim Flash-Schreiben einen Reset auslösen, der den Upload mittendrin abbricht (die alte Firmware bleibt dabei unangetastet, ein erneuter Versuch ist jederzeit möglich). Update deshalb möglichst bei über ESC/Akku bestromtem Modul durchführen, nicht nur über USB.
- **Nach dem Update:** Das Modul startet automatisch neu, die WLAN-Verbindung bricht dabei kurz ab – das ist normal. Anschließend im WLAN neu verbinden und die Funktion prüfen.
- **Falls das Webinterface danach nicht mehr erreichbar ist:** Modul kurz von der Spannungsversorgung trennen und neu starten. Sollte die neue Firmware fehlerhaft sein, hilft in letzter Konsequenz ein reguläres Flashen per USB mit einer bekannt funktionierenden Version.

---

## Änderungen v1.42 (übertragen aus Soundmodul v1.24)

Diese Version macht das Multiswitch für den **Multi-Device-Betrieb** am selben Empfänger tauglich:

- **Eindeutige CRSF-Geräteadresse** aus der Modul-Adresse (`0xC0 + modul_adress`) statt fest 0xC8 – dadurch kollidiert das Multiswitch nicht mehr mit anderen CRSF-Konfigurationsgeräten (z. B. dem Soundmodul), die sich sonst ebenfalls als 0xC8 melden
- **Ping-Answer-Slot** (`Slot = (Adresse−0xC0)×2`, Verzögerung `Slot × CRSF_SLOT_MS`) – die device_info-Antwort wird im eigenen Zeit-Slot gesendet, sodass sich die Antworten mehrerer Module auf dem Downlink nicht überlagern
- **Versionskonsistenz** auf `v1.42` (Firmware-Konstante, Web-UI-Header, README)

*Nicht übertragen (in dieser Version nicht nötig):* RxBt-Korrektur (das Multiswitch sendet keinen Batterie-Frame), Parameter-Lücken-Fix (IDs 0–76 bereits lückenlos), S.Port-Absturz-Schutz (kein S.Port vorhanden).

**Voraussetzung Multi-Device:** unterschiedliche Modul-Adressen je Gerät und ein Empfänger mit ExpressLRS ab Version 4.x.

---

## Änderungen v1.41

- **CRSF Parser-Fix**: Doppelschreiben des CRC-Bytes im RX-Parser entfernt
- **CRSF Init-Fix**: `init_crsf()` verwendet nun den übergebenen Serial-Port
- **CRSF API-Fix**: `send_command()` korrekt als Klassenmethode implementiert
- **Web-API robuster**: JSON-Strings werden sicher escaped, Eingaben werden valider geparst/validiert
- **NVS-Schreibschutz**: Konfigurationsänderungen werden gebündelt gespeichert (Debounce), Flush vor Neustart
- **Versionskonsistenz**: Firmware, Web-UI und README auf `v1.41` vereinheitlicht

## Pin-Belegung

| GPIO | Funktion |
|------|----------|
| 13 | WiFi-Pin (LOW beim Booten = AP aktiv; AP kann ab v2.00 auch per Web oder Auto-Failsafe zur Laufzeit gestartet werden) |
| 16 | SBUS RX / CRSF RX |
| 17 | CRSF TX |
| 18–27 | Ausgänge 1–8 |
| 2 | Status-LED |

## Bibliotheken

- Bolder Flight Systems SBUS 8.1.4
- CRSF_ESP32 (https://github.com/Ziege-One/CRSF_ESP32)

## RC-System Werte

| Wert | System |
|------|--------|
| 0 | FrSky (SBUS) |
| 1 | FlySky (SBUS) |
| 2 | ELRS normiert (SBUS) |
| 3 | HoTT (SBUS) |
| 4 | CRSF (ELRS/TBS) |

## Web-Interface

IP: `192.168.1.1` (Standard)
SSID: `MultiSwitch` / Passwort: `123456789`

### API-Endpunkte

| Methode | Pfad | Beschreibung |
|---------|------|--------------|
| GET | `/api/status` | Aktueller Status (Ausgänge, Kanäle, MWprop, CRSF-Diagnose, AP-Status) |
| GET/POST | `/api/config` | Konfiguration lesen/schreiben (inkl. WLAN-Auto-Failsafe) |
| POST | `/api/switch` | Ausgang manuell schalten / freigeben |
| POST | `/api/output` | Ausgangs-Konfiguration (Kanal, PWM, Blink-Modus, Name) |
| POST | `/api/wifi` | *(NEU v2.00)* Access Point sofort ein-/ausschalten, ohne Neustart |
| POST | `/api/save` | *(NEU v2.00)* Konfiguration sofort auf Flash schreiben, ohne Neustart |
| POST | `/api/otaupdate` | *(NEU v2.00)* Firmware-Update per WLAN hochladen (multipart/form-data, `.bin`-Datei) – startet das Modul bei Erfolg neu |
| POST | `/api/reset` | Werkseinstellungen |
| POST | `/api/restart` | Neustart (nach Sichern) |
