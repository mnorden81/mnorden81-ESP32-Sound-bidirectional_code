#ifndef PORT_FUNCTION_H
#define PORT_FUNCTION_H

/*
 * port_function.h  –  ESP32-RC-Sound (Port-B-Rollen-Registry)
 *
 * Registriert jede Port-B-Rolle (siehe enum PortBMode in config.h) als
 * {name, defaultBaud, begin, update, isActive}-Eintrag. setup()/loop() im
 * .ino kennen danach keine rollenspezifischen Details mehr - sie rufen nur
 * noch getPortBModule(config.PortB_Mode)->begin(...)/->update() auf. Web-UI
 * und CRSF/Lua-Menue lesen ihre Optionsliste (portBFunctionOptionsString())
 * ebenfalls aus dieser einen Tabelle, statt sie an drei Stellen (HTML,
 * JS, CRSF-TEXT_SELECTION-String) von Hand synchron zu halten.
 *
 * Aktuell ist nur Port B generisch waehlbar (Port A ist fest RC-Eingang,
 * Port C ist fest GPS via SoftwareSerial) - die Registry ist trotzdem als
 * eigenstaendiges Modul angelegt, damit sich ein spaeterer zweiter
 * generischer Port oder weitere Port-B-Rollen ergaenzen lassen, ohne
 * setup()/loop()/Web-UI anzufassen.
 *
 * Port B braucht zusaetzlich eine (a) waehlbare Baudrate und (b) den von
 * portBFreeSerial() ermittelten freien Hardware-UART als Parameter - begin()
 * bekommt daher HardwareSerial*, rxPin, txPin UND baud.
 */

#include <Arduino.h>
#include "config.h"   // enum PortBMode, PORTB_FUNCTION_COUNT, PORTB_BAUD_DEFAULT_*

struct PortBFunctionModule {
    const char* name;          // Anzeigename (Web/CRSF), auch fuer "Aus"
    uint32_t    defaultBaud;   // beim Umschalten auf diese Rolle vorbelegt (0 = unveraendert lassen, siehe PORTB_OFF)
    // serial: der von portBFreeSerial() (im .ino) ermittelte freie Hardware-
    // UART. rxPin/txPin: physisch immer 32/33 (Port B ist fest verdrahtet),
    // als Parameter statt Define, damit begin() nicht an ein bestimmtes
    // Pin-Paar gebunden ist.
    void (*begin)(HardwareSerial* serial, uint8_t rxPin, uint8_t txPin, uint32_t baud);
    void (*update)();
    bool (*isActive)();
};

// Liefert das Modul fuer eine Port-B-Rolle, oder nullptr fuer PORTB_OFF
// oder einen unbekannten/ungueltigen Wert (siehe clampPortBMode()).
const PortBFunctionModule* getPortBModule(PortBMode f);

// Anzeigename einer Rolle (auch fuer PORTB_OFF: "Aus").
const char* portBFunctionName(PortBMode f);

// ";"-getrennte Namensliste aller Rollen in Enum-Reihenfolge, z.B.
// "Aus;CRSF;S.Port;Hott (bald);Raw" - fuer CRSF_TEXT_SELECTION und das
// Web-Dropdown identisch nutzbar (Index in der Liste == numerischer
// PortBMode-Wert). Einmal hier gepflegt statt an mehreren Stellen von Hand.
const char* portBFunctionOptionsString();

// Protokolltypischer Standard-Baudwert einer Rolle (0 fuer PORTB_OFF oder
// einen unbekannten Wert - Aufrufer soll die Baudrate dann unveraendert
// lassen). Ersetzt einen sonst doppelt zu pflegenden switch/case im .ino
// UND das PORTB_BAUD_DEFAULTS-Objekt im Web-UI-JavaScript.
uint32_t portBFunctionDefaultBaud(PortBMode f);

// Groesster gueltiger Auswahl-Index (PORTB_FUNCTION_COUNT - 1).
static inline uint8_t portBFunctionMaxIndex() { return PORTB_FUNCTION_COUNT - 1; }

// Auf einen gueltigen PortBMode begrenzt (unbekannte/alte NVS-Werte -> Aus).
PortBMode clampPortBMode(int raw);

#endif // PORT_FUNCTION_H
