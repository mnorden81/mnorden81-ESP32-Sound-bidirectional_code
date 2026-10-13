// crsf_platform.h - CRSF_ESP32
//
// Auf dem ESP32 (ARDUINO definiert) einfach <Arduino.h>. Auf dem Host
// (PlatformIO "native", Unity-Tests) ein minimaler Ersatz fuer die wenigen
// Arduino-Teile, die die Bibliothek benutzt: Stream (UART), millis(),
// byte, HEX und ein stummes Serial fuer die Debug-Ausgaben. Damit laesst
// sich die komplette CRSF-Klasse ohne Hardware testen (test/test_crsf).

#ifndef CRSF_PLATFORM_H
#define CRSF_PLATFORM_H

#ifdef ARDUINO

#include <Arduino.h>

#else

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <initializer_list>

typedef uint8_t byte;

#ifndef HEX
#define HEX 16
#endif

// Entspricht dem, was die Bibliothek von Arduinos Stream nutzt.
class Stream {
public:
    virtual ~Stream() {}
    virtual int available() = 0;
    virtual int read() = 0;
    virtual size_t write(const uint8_t* buffer, size_t size) = 0;
};

// Host-Zeit fuer Tests; crsf_set_millis() stellt sie ein.
unsigned long millis();
void crsf_set_millis(unsigned long ms);

// Debug-Ausgaben verschwinden auf dem Host.
struct CrsfNullSerial {
    template <typename... A> void print(A...) {}
    template <typename... A> void println(A...) {}
    template <typename... A> void printf(A...) {}
};
extern CrsfNullSerial Serial;

#endif // ARDUINO

#endif // CRSF_PLATFORM_H
