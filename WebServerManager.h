// WebServerManager.h  –  ESP32-RC-Sound
#pragma once
#include <WiFi.h>
#include <WebServer.h>

class WebServerManager {
public:
  // begin() registriert nur die Routen und merkt sich SSID/Passwort - wird
  // IMMER in setup() aufgerufen (unabhaengig vom WifiPin/GPIO13-Zustand),
  // damit WLAN auch nachtraeglich gestartet werden kann. Das tatsaechliche
  // Ein-/Ausschalten von AP-Funk + Webserver-Socket uebernehmen
  // enableAP()/disableAP() - aufrufbar sowohl beim Booten (falls WifiPin
  // beim Start auf LOW lag) als auch spaeter zur Laufzeit (manueller
  // Schalter im Web/Lua-Menue oder automatisch bei RC-Signalverlust, siehe
  // wifiFailsafeCheck() in ESP32-RC-Sound.ino). Public, da direkt aus dem
  // .ino aufgerufen.
  static void begin(const char* apSsid, const char* apPassword);
  static void enableAP();
  static void disableAP();
  static bool isApActive();
  static void Webpage();

private:
  static WebServer server;
  static int       Menu;
  static String    valueString;
  static void      handleRequest();
  static void      handleSport();
  static void      handleApiConfig();
  static void      handleApiConfigPost();
  static void      handleApiDebug();
  static void      handleApiSound();
  // Konfiguration direkt auf/von der bereits vorhandenen SD-Karte
  // sichern/wiederherstellen (zusaetzlich zum Browser-Download/-Upload,
  // siehe Kommentare in WebServerManager.cpp).
  static String    buildConfigJson();
  static void      handleApiSdExport();
  static void      handleApiSdList();
  static void      handleApiSdConfig();
  // Sound-WAV-Dateien per WLAN direkt auf die SD-Karte hochladen
  // (multipart/form-data, siehe Kommentare in WebServerManager.cpp).
  static void      handleApiSoundUpload();
  static void      handleApiSoundUploadData();
  // Datei-Verwaltung - Status aller Sound-Dateien abfragen, gezielt eine
  // Sound-Datei oder ein Config-Backup wieder loeschen (siehe Kommentare
  // in WebServerManager.cpp).
  static void      handleApiSoundFiles();
  static void      handleApiSoundDelete();
  static void      handleApiSdDelete();
  // Firmware-Update direkt per WLAN (OTA) - .bin-Datei per
  // multipart/form-data hochladen, ueber die Arduino-"Update"-Bibliothek
  // ins jeweils inaktive OTA-Flash-Segment schreiben, dann neu starten
  // (siehe Kommentare in WebServerManager.cpp). Braucht eine OTA-faehige
  // Partitionstabelle - siehe platformio.ini.
  static void      handleApiOtaUpdate();
  static void      handleApiOtaUpdateData();
  // Manueller WLAN-Schalter im Web-Tab "WiFi" (entspricht Lua-Feld 174) -
  // schaltet enableAP()/disableAP() sofort, ohne Neustart und ohne die
  // Konfiguration zu aendern (siehe Kommentare in WebServerManager.cpp).
  static void      handleApiWifiEnable();
  static String    urlDecode(const String& s);
};
