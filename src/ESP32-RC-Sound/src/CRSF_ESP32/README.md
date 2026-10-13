# CRSF_ESP32

Gemeinsame CRSF-Bibliothek für das ESP32-Soundmodul und den ESP32-Multiswitch (V2, V3, TXRX). Basis ist [CRSF_ESP32 von Ziege-One](https://github.com/Ziege-One/CRSF_ESP32) (GPL-3.0).

## Wo sie liegt

Jedes Projekt trägt eine **identische Kopie** dieses Ordners im Sketch unter `src/CRSF_ESP32/`. Die Arduino-IDE und PlatformIO kompilieren den Ordner automatisch mit, es muss nichts installiert werden. Eingebunden wird sie mit:

```cpp
#include "src/CRSF_ESP32/crsf_esp32.h"
```

| Projekt | Ordner |
|---|---|
| Soundmodul | `src/ESP32-RC-Sound/src/CRSF_ESP32/` |
| Multiswitch V2 | `MultiSwitch_ESP32/src/CRSF_ESP32/` |
| Multiswitch V3 | `MultiSwitch_ESP32_V3/src/CRSF_ESP32/` |
| Multiswitch TXRX | `MultiSwitch_ESP32_TXRX/src/CRSF_ESP32/` |

**Regel:** Eine Änderung immer in allen Kopien nachziehen und `CRSF_ESP32_VERSION` in `crsf_esp32.h` hochzählen. Gleiche Versionsnummer heißt gleicher Inhalt. Die Host-Tests liegen im Soundmodul-Repo (`test/test_crsf`, `pio test -e native`).

## Dateien

| Datei | Inhalt |
|---|---|
| `crsf_esp32.h/.cpp` | Klasse `CRSF`: Empfang, Parameter-Menü (Lua), Telemetrie, WM-Multiswitch-Kommandos |
| `crsf_esp32_protocol.h` | Frame-Typen, Adressen, Datentypen, Strukturen `LinkStatistics` und `DeviceInfo` |
| `crsf_platform.h` | `<Arduino.h>` auf dem ESP32, kleiner Ersatz auf dem Host für die Tests |

## Versionen

| Version | Änderung |
|---|---|
| 1.0.0 | Zusammengeführt aus Soundmodul v7.18 und Multiswitch v2.03/v3.12. Fixes laut CRSF-Spezifikation: Parser behält seinen Zustand über `read_packets()`-Aufrufe (halb empfangene Frames gingen verloren und zählten als CRC-Fehler); Link-Statistik 0x14 mit `uplinkRSSI2`, alle Felder korrekt zugeordnet; Command-CRC-Tabelle (0xBA) korrigiert; `send_command_MWPROP` mit Command-CRC; FOLDER-Eintrag ohne überzähliges Byte; ungenutztes `send_command()` entfernt. Neu: `getLinkStatistics()`, `getDeviceInfo()`, `attach_stream()`. |
