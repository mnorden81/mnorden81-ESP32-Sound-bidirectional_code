// WebServerManager.cpp - Weboberflaeche, REST-API und OTA-Update fuer
// ESP32-RC-Sound. Das komplette UI (HTML/CSS/JS) liegt im Flash (PROGMEM,
// RCSOUND_HTML) - server.send_P() streamt es direkt ohne RAM-Kopie oder
// String-Allokation pro Request.
#include "WebServerManager.h"
#include "config.h"
#include <Arduino.h>
#include "XT_I2S_Audio.h"
#include "sport_lipo.h"
#include "gps_speed.h"
#include "crsf_esp32.h"
#include "port_function.h"   // Port-B-Rollen-Registry
#include <Update.h>          // Firmware-Update per WLAN (OTA), siehe handleApiOtaUpdate*()

// Zugriff auf die CRSF-Instanz aus dem Hauptsketch (fuer Diagnose-Zaehler)
extern CRSF crsf;
extern bool gpsPinConflict; // SBUS+GPS GPIO27-Konflikt, siehe ESP32-RC-Sound.ino setup()
// Die tatsaechlich AKTIVEN (seit dem letzten Neustart geladenen) Port B/C-
// Werte - koennen von config.PortB_Mode/PortC_GPS_Enabled abweichen, wenn
// seitdem ueber Web/Lua etwas geaendert, aber noch nicht neu gestartet
// wurde (siehe Erklaerung in ESP32-RC-Sound.ino).
extern int PortB_Mode_boot;
extern int PortC_GPS_Enabled_boot;

// ── PROGMEM HTML ─────────────────────────────────────────────────────────
// Das komplette UI liegt im Flash (PROGMEM). server.send_P() streamt es
// direkt ohne RAM-Kopie. Keine String-Allokation mehr pro Request.
static const char RCSOUND_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 RC-Sound</title>
<style>
:root{
  --bg:#0d1117;--surf:#161b22;--surf2:#21262d;
  --border:#30363d;--accent:#4493f8;--green:#3fb950;
  --yellow:#d29922;--red:#f85149;--text:#e6edf3;
  --sub:#8b949e;--r:8px;--mono:'Courier New',monospace;
}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;font-size:14px}
header{background:var(--surf);border-bottom:1px solid var(--border);padding:12px 16px;display:flex;align-items:center;position:sticky;top:0;z-index:50}
header h1{font-size:16px;font-weight:700;flex:1}
.hv{font-size:11px;color:var(--sub)}
nav{background:var(--surf);border-bottom:1px solid var(--border);display:flex;overflow-x:auto;scrollbar-width:none;position:sticky;top:45px;z-index:40}
nav::-webkit-scrollbar{display:none}
.tab{flex-shrink:0;padding:10px 14px;font-size:12px;font-weight:600;color:var(--sub);cursor:pointer;border-bottom:2px solid transparent;transition:.2s;white-space:nowrap}
.tab:hover{color:var(--text)}
.tab.active{color:var(--accent);border-bottom-color:var(--accent)}
main{max-width:560px;margin:0 auto;padding:14px 14px 60px}
.card{background:var(--surf);border:1px solid var(--border);border-radius:var(--r);padding:14px;margin-bottom:10px}
.card-title{font-weight:600;font-size:11px;color:var(--sub);text-transform:uppercase;letter-spacing:.05em;margin-bottom:10px}
.row{display:flex;align-items:center;gap:8px;margin-bottom:8px}
.lbl{font-size:13px;color:var(--sub);flex:0 0 130px}
select,input[type=text],input[type=password],input[type=number]{
  width:100%;background:var(--surf2);border:1px solid var(--border);
  border-radius:var(--r);color:var(--text);padding:8px 10px;font-size:13px;outline:none;transition:border .2s}
select:focus,input:focus{border-color:var(--accent)}
.sl-wrap{margin-bottom:12px}
.sl-head{display:flex;justify-content:space-between;font-size:12px;color:var(--sub);margin-bottom:5px}
input[type=range]{width:100%;-webkit-appearance:none;height:4px;border-radius:2px;background:var(--surf2);outline:none}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:18px;height:18px;border-radius:50%;background:var(--accent);cursor:pointer}
input[type=range]::-moz-range-thumb{width:18px;height:18px;border-radius:50%;background:var(--accent);cursor:pointer;border:none}
.btn{display:inline-flex;align-items:center;justify-content:center;padding:9px 16px;border-radius:var(--r);font-size:13px;font-weight:600;cursor:pointer;border:none;transition:.15s;width:100%;margin-top:6px}
.btn-p{background:var(--accent);color:#fff}.btn-p:hover{opacity:.85}
.btn-g{background:var(--green);color:#fff}.btn-g:hover{opacity:.85}
.btn-r{background:var(--red);color:#fff}.btn-r:hover{opacity:.85}
.btn-s{background:var(--surf2);color:var(--text);border:1px solid var(--border)}.btn-s:hover{border-color:var(--accent);color:var(--accent)}
.g2{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.badge{display:inline-block;padding:2px 8px;border-radius:20px;font-size:11px;font-weight:600}
.bg-g{background:#1a3a1a;color:var(--green)}
.bg-r{background:#3a1a1a;color:var(--red)}
.bg-s{background:var(--surf2);color:var(--sub)}
.stabs{display:flex;gap:4px;flex-wrap:wrap;margin-bottom:12px}
.stab{padding:5px 10px;border-radius:var(--r);font-size:12px;font-weight:600;cursor:pointer;background:var(--surf2);color:var(--sub);border:1px solid var(--border);transition:.15s}
.stab.active{background:var(--accent);color:#fff;border-color:var(--accent)}
.cell-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:6px;margin-top:8px}
.cell-box{background:var(--surf2);border-radius:var(--r);padding:8px;text-align:center}
.cell-v{font-size:17px;font-weight:700}
.cell-l{font-size:10px;color:var(--sub);margin-top:2px}
.dbg-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:4px}
.dbg-item{background:var(--surf2);border-radius:4px;padding:6px 8px}
.dbg-k{font-size:10px;color:var(--sub)}
.dbg-v{font-size:13px;font-weight:600;margin-top:1px}
hr{border:none;border-top:1px solid var(--border);margin:10px 0}
.mono{font-family:var(--mono);font-size:12px}
.hidden{display:none!important}
/* Toast */
#toast{position:fixed;bottom:24px;left:50%;transform:translateX(-50%);
  background:var(--green);color:#fff;padding:10px 20px;border-radius:var(--r);
  font-size:13px;font-weight:600;opacity:0;transition:opacity .3s;pointer-events:none;z-index:99}
#toast.err{background:var(--red)}
#toast.show{opacity:1}
</style>
</head>
<body>
<header>
  <h1>&#9654; ESP32 RC-Sound</h1>
  <span class="hv" id="hv">laden...</span>
</header>
<nav>
  <div class="tab active" onclick="showTab('motor')">Motor</div>
  <div class="tab" onclick="showTab('sounds')">Sounds</div>
  <div class="tab" onclick="showTab('settings')">Einstellung</div>
  <div class="tab" onclick="showTab('wifi')">WiFi</div>
  <div class="tab" onclick="showTab('lipo')">LiPo</div>
  <div class="tab" onclick="showTab('debug')">Debug</div>
</nav>
<div id="toast"></div>
<main>

<!-- ═══ MOTOR ═══════════════════════════════════════════════════════════ -->
<div id="tab-motor">
  <div class="card">
    <div class="card-title">Motor Mode</div>
    <select id="m-mode">
      <option value="0">Eine Richtung</option>
      <option value="1">Zwei Richtungen</option>
    </select>
  </div>
  <div class="card">
    <div class="card-title">Motor EIN Modus</div>
    <select id="m-toggle">
      <option value="0">Normal</option>
      <option value="1">Toggle</option>
    </select>
  </div>
  <div class="card">
    <div class="card-title">Quelle Einschalten Motor</div>
    <select id="m-src"></select>
  </div>
  <div class="card">
    <div class="card-title">Quelle Motorspeed</div>
    <select id="m-spd"></select>
  </div>
  <div class="card">
    <div class="card-title">Lautst&auml;rke &amp; Drehzahl</div>
    <div class="sl-wrap">
      <div class="sl-head"><span>Volumen</span><span id="lv-m-vol">-</span></div>
      <input type="range" id="m-vol" min="0" max="200" step="5" oninput="slUpd('m-vol','lv-m-vol')">
    </div>
    <div class="sl-wrap">
      <div class="sl-head"><span>Drehzahl min %</span><span id="lv-m-rmin">-</span></div>
      <input type="range" id="m-rmin" min="0" max="200" step="5" oninput="slUpd('m-rmin','lv-m-rmin')">
    </div>
    <div class="sl-wrap">
      <div class="sl-head"><span>Drehzahl max %</span><span id="lv-m-rmax">-</span></div>
      <input type="range" id="m-rmax" min="100" max="600" step="5" oninput="slUpd('m-rmax','lv-m-rmax')">
    </div>
    <div class="sl-wrap">
      <div class="sl-head"><span>Standgas Verz&ouml;gerung s</span><span id="lv-m-sdly">-</span></div>
      <input type="range" id="m-sdly" min="0" max="60" step="2" oninput="slUpd('m-sdly','lv-m-sdly')">
    </div>
    <div class="sl-wrap">
      <div class="sl-head"><span>Motor Rampe %/s</span><span id="lv-m-ramp">-</span></div>
      <input type="range" id="m-ramp" min="0" max="50" step="2" oninput="slUpd('m-ramp','lv-m-ramp')">
    </div>
    <div class="sl-wrap">
      <div class="sl-head"><span>Standgas Totband %</span><span id="lv-m-dead">-</span></div>
      <input type="range" id="m-dead" min="0" max="50" step="2" oninput="slUpd('m-dead','lv-m-dead')">
    </div>
  </div>
  <div class="g2">
    <button class="btn btn-p" onclick="saveMotor()">Speichern</button>
    <button class="btn btn-s" onclick="loadConfig()">Zur&uuml;cksetzen</button>
  </div>
  <div class="card" style="margin-top:10px">
    <div class="card-title">Voreinstellungen</div>
    <div class="g2">
      <button class="btn btn-s" onclick="xget('/setsbus',0,function(){loadConfig();})">SBUS</button>
      <button class="btn btn-s" onclick="xget('/setpwm',0,function(){loadConfig();})">PWM</button>
    </div>
    <button class="btn btn-s" style="margin-top:6px" onclick="xget('/setpin',0,function(){loadConfig();})">PIN</button>
    <button class="btn btn-r" style="margin-top:6px" onclick="if(confirm('Werkseinstellung laden?'))xget('/reset',0,function(){loadConfig();})">Werkseinstellung</button>
  </div>
  <!-- Loop-/Standgas-/Start-Sound-Dateien verwalten (Upload+Loeschen),
       analog zur Sound-Datei-Verwaltung im Tab "Sounds". -->
  <div class="card" style="margin-top:10px">
    <div class="card-title">Motor-Sound-Dateien verwalten</div>
    <div style="font-size:12px;color:var(--sub);margin-bottom:10px">
      Loop-, Standgas- und Start-Sound direkt per WLAN auf die SD-Karte hochladen
      oder von dort l&ouml;schen - wirkt sofort, ohne Neustart des Moduls.
    </div>
    <div class="sl-wrap">
      <div class="sl-head"><span>Loop (loop.wav)</span></div>
      <div style="font-size:12px;color:var(--sub);margin-bottom:6px" id="mf-loop-status">-</div>
      <div class="g2">
        <button class="btn btn-s" onclick="document.getElementById('mf-loop-file').click()">&#8679; Hochladen</button>
        <button class="btn btn-r" onclick="deleteMotorFile('loop.wav')">L&ouml;schen</button>
      </div>
      <input type="file" id="mf-loop-file" accept="audio/wav,.wav" style="display:none" onchange="uploadMotorFile('loop.wav',this.files[0])">
    </div>
    <div class="sl-wrap">
      <div class="sl-head"><span>Standgas (shut.wav)</span></div>
      <div style="font-size:12px;color:var(--sub);margin-bottom:6px" id="mf-shut-status">-</div>
      <div class="g2">
        <button class="btn btn-s" onclick="document.getElementById('mf-shut-file').click()">&#8679; Hochladen</button>
        <button class="btn btn-r" onclick="deleteMotorFile('shut.wav')">L&ouml;schen</button>
      </div>
      <input type="file" id="mf-shut-file" accept="audio/wav,.wav" style="display:none" onchange="uploadMotorFile('shut.wav',this.files[0])">
    </div>
    <div class="sl-wrap">
      <div class="sl-head"><span>Start (start.wav)</span></div>
      <div style="font-size:12px;color:var(--sub);margin-bottom:6px" id="mf-start-status">-</div>
      <div class="g2">
        <button class="btn btn-s" onclick="document.getElementById('mf-start-file').click()">&#8679; Hochladen</button>
        <button class="btn btn-r" onclick="deleteMotorFile('start.wav')">L&ouml;schen</button>
      </div>
      <input type="file" id="mf-start-file" accept="audio/wav,.wav" style="display:none" onchange="uploadMotorFile('start.wav',this.files[0])">
    </div>
    <div class="row hidden" id="mf-upload-progress">
      <div style="font-size:12px;color:var(--sub)" id="mf-upload-status">-</div>
    </div>
  </div>
</div>

<!-- ═══ SOUNDS ══════════════════════════════════════════════════════════ -->
<div id="tab-sounds" class="hidden">
  <div class="stabs" id="stabs"></div>
  <div class="card">
    <div class="card-title">Quelle Einschalten</div>
    <select id="s-src"></select>
  </div>
  <div class="card">
    <div class="card-title">Lautst&auml;rke</div>
    <div class="sl-wrap">
      <div class="sl-head"><span>Volumen</span><span id="lv-s-vol">-</span></div>
      <input type="range" id="s-vol" min="0" max="200" step="5" oninput="slUpd('s-vol','lv-s-vol')">
    </div>
  </div>
  <div class="card">
    <div class="card-title">Wiedergabe Modus</div>
    <select id="s-mode">
      <option value="0">Normal</option>
      <option value="1">Loop</option>
      <option value="2">Tippbetrieb</option>
    </select>
  </div>
  <div class="g2">
    <button class="btn btn-p" onclick="saveSound()">Speichern</button>
    <button class="btn btn-g" onclick="testSound()">&#9654; Test Sound</button>
  </div>
  <!-- WAV-Datei fuer den aktuell ausgewaehlten Sound-Slot direkt per WLAN auf
       die SD-Karte im Modul hochladen - kein Ausbau der Karte noetig. Nutzt
       den bestehenden SD-Mutex (siehe handleApiSoundUploadData()), damit
       kein Konflikt mit laufender Sound-Wiedergabe entsteht. Zusaetzlich
       Status (vorhanden/Groesse) und Loeschen. -->
  <div class="card">
    <div class="card-title">Sound-Datei (WAV) verwalten</div>
    <div style="font-size:12px;color:var(--sub);margin-bottom:6px">
      Sound <span id="snd-upload-label">-</span> (<code id="snd-file-name">-</code>)
    </div>
    <div style="font-size:12px;color:var(--sub);margin-bottom:10px" id="snd-file-status">-</div>
    <div class="g2">
      <button class="btn btn-s" onclick="document.getElementById('snd-upload-file').click()">&#8679; WAV hochladen</button>
      <button class="btn btn-r" onclick="deleteSoundSlotFile()">L&ouml;schen</button>
    </div>
    <input type="file" id="snd-upload-file" accept="audio/wav,.wav" style="display:none" onchange="uploadSoundFile(this.files[0])">
    <div class="row hidden" id="snd-upload-progress" style="margin-top:8px">
      <div style="font-size:12px;color:var(--sub)" id="snd-upload-status">-</div>
    </div>
    <div style="font-size:12px;color:var(--sub);margin-top:10px">
      Ersetzt bzw. l&ouml;scht die WAV-Datei direkt auf der SD-Karte im Modul.
      Erwartet 16-Bit-PCM-WAV; die &Auml;nderung wirkt sofort, ohne Neustart.
    </div>
  </div>
</div>

<!-- ═══ EINSTELLUNGEN ═══════════════════════════════════════════════════ -->
<div id="tab-settings" class="hidden">
  <div class="card">
    <div class="card-title">RC-System</div>
    <select id="rc-sys" onchange="onRcSys()">
      <option value="0">FrSky</option>
      <option value="1">FlySky</option>
      <option value="2">ELRS (SBUS)</option>
      <option value="3">Hott</option>
      <option value="4">ELRS (CRSF)</option>
    </select>
  </div>
  <div class="card" id="c-ekch">
    <div class="card-title">Einkanal Kanal</div>
    <select id="ek-ch"></select>
  </div>
  <div class="card" id="c-ekmo">
    <div class="card-title">Einkanal-Mode</div>
    <select id="ek-mode">
      <option value="0">Normal</option>
      <option value="10">SBUS WM Adr 0</option>
      <option value="11">SBUS WM Adr 1</option>
      <option value="12">SBUS WM Adr 2</option>
      <option value="13">SBUS WM Adr 3</option>
    </select>
  </div>
  <div class="card hidden" id="c-madr">
    <div class="card-title">Modul Adresse (CRSF)</div>
    <select id="mod-adr"></select>
  </div>
  <div class="card hidden" id="c-mkan">
    <div class="card-title">Mehrfachadress-Erweiterung (MKan)</div>
    <div class="row" style="margin-bottom:10px">
      <span class="lbl">Gruppe 1 (MKan 01-08)</span>
      <select id="mkan-adr0"></select>
    </div>
    <div class="row">
      <span class="lbl">Gruppe 2 (MKan 09-16)</span>
      <select id="mkan-adr1"></select>
    </div>
  </div>
  <div class="card hidden" id="c-sbusgrp">
    <div class="card-title">Mehrfachadress-Erweiterung (MKan, SBUS)</div>
    <div class="row" style="margin-bottom:6px">
      <span class="lbl">Gruppe 1 Kanal (MKan 01-08)</span>
      <select id="sbusgrp-ch0"></select>
    </div>
    <div class="row" style="margin-bottom:10px">
      <span class="lbl">Gruppe 1 Modus</span>
      <select id="sbusgrp-mode0">
        <option value="0">Normal</option>
        <option value="10">SBUS WM Adr 0</option>
        <option value="11">SBUS WM Adr 1</option>
        <option value="12">SBUS WM Adr 2</option>
        <option value="13">SBUS WM Adr 3</option>
      </select>
    </div>
    <div class="row" style="margin-bottom:6px">
      <span class="lbl">Gruppe 2 Kanal (MKan 09-16)</span>
      <select id="sbusgrp-ch1"></select>
    </div>
    <div class="row">
      <span class="lbl">Gruppe 2 Modus</span>
      <select id="sbusgrp-mode1">
        <option value="0">Normal</option>
        <option value="10">SBUS WM Adr 0</option>
        <option value="11">SBUS WM Adr 1</option>
        <option value="12">SBUS WM Adr 2</option>
        <option value="13">SBUS WM Adr 3</option>
      </select>
    </div>
    <div style="font-size:12px;color:var(--sub);margin-top:10px">
      Nutzt je Gruppe einen eigenen SBUS-Kanal (Deaktiviert = kein Kanal
      zugewiesen) - anders als MKan bei CRSF/ELRS belegt das zusaetzliche
      RC-Kanaele, da SBUS keine adressierten Pakete kennt.
    </div>
  </div>
  <hr>
  <div class="card" id="c-pwm">
    <div class="card-title">PWM Einstellungen</div>
    <div class="row" style="margin-bottom:10px">
      <span class="lbl">PWM min (&micro;s)</span>
      <input type="number" id="pwm-min" min="0" max="4095" style="width:120px">
    </div>
    <div class="row">
      <span class="lbl">PWM max (&micro;s)</span>
      <input type="number" id="pwm-max" min="0" max="4095" style="width:120px">
    </div>
  </div>

  <hr>
  <div class="card">
    <div class="card-title">Hardware Config</div>
    <select id="hw-cfg" onchange="onHwCfg()">
      <option value="0">V1 (GPIO 16,17,22,0,2,4)</option>
      <option value="1">V2 (GPIO 16,17,14,27,32,33)</option>
      <option value="2">V3 (nur BUS+Einkanal, + Port B/C)</option>
    </select>
    <!-- Zeigt "(Neustart noetig!)", sobald Hardware_Config_Pending vom aktiv
         laufenden Hardware_Config abweicht (hardwareConfigRestartPending(),
         siehe config.h) - derselbe Hinweis, den auch das CRSF/Lua-Menue am
         Sender zeigt. Der Wert kommt ueber /api/config als "restart_pending"
         im JSON. -->
    <div id="hw-restart-hint" class="hidden" style="color:var(--yellow);font-size:12px;margin-top:8px">
      &#9888; Neustart erforderlich, damit die neue Hardware Config aktiv wird.
    </div>
  </div>
  <hr>
  <!-- Port B (echter 2. Hardware-UART, GPIO32/33) und Port C (GPS,
       SoftwareSerial GPIO27/14) sind unabhaengig von der Hardware-Version
       waehlbar - nur sichtbar/wirksam auf Hardware V3 (siehe onHwCfg()), da
       V1/V2 diese Pins fuer PWM-Eingaenge brauchen. -->
  <div class="card" id="c-portbc">
    <div class="card-title">Port B / Port C</div>
    <div class="row" style="margin-bottom:10px">
      <span class="lbl">Port B Modus (GPIO32/33)</span>
      <!-- Optionen kommen zur Laufzeit vom Geraet (s.portb_options in
           /api/config, siehe fillSelFlat() weiter unten) statt hier UND in
           port_function.cpp UND im CRSF/Lua-Menue von Hand dreifach
           identisch gepflegt zu werden. -->
      <select id="portb-mode" onchange="onPortBMode()"></select>
    </div>
    <div class="row" id="row-portb-baud" style="margin-bottom:10px">
      <span class="lbl">Port B Baudrate</span>
      <input type="number" id="portb-baud" min="300" max="2000000" style="width:120px">
    </div>
    <div class="row">
      <span class="lbl">Port C (GPS, GPIO27/14)</span>
      <select id="portc-gps">
        <option value="0">Aus</option>
        <option value="1">An</option>
      </select>
    </div>
    <div style="color:var(--yellow);font-size:12px;margin-top:8px">
      &#9888; Aenderungen an Port B/C wirken erst nach einem Neustart des Moduls.
      Port C (GPS) kollidiert mit SBUS (GPIO27 wird von beiden belegt) - fuer
      GPS bitte Einkanal/RC-System auf CRSF stellen.
    </div>
  </div>
  <button class="btn btn-p" onclick="saveSettings()">Speichern</button>

  <!-- Konfiguration Export/Import: komplette Geraetekonfiguration (Sounds,
       Motor, RC-System, Port B/C, WLAN, ...) als JSON sichern bzw. aus einer
       zuvor exportierten Datei wiederherstellen - z.B. vor einem Firmware-
       Update oder um mehrere Module baugleich zu konfigurieren. -->
  <div class="card" id="c-cfgbackup" style="margin-top:10px">
    <div class="card-title">Konfiguration sichern / wiederherstellen</div>
    <div style="font-size:12px;color:var(--sub);margin-bottom:10px">
      Export speichert die komplette Konfiguration (alle Sounds, Motor,
      RC-System, Port B/C, WLAN, ...) als JSON-Datei auf diesem Geraet.
      Import spielt eine zuvor exportierte Datei wieder ein.
    </div>
    <div class="g2">
      <button class="btn btn-s" onclick="exportConfig()">&#8681; Export</button>
      <button class="btn btn-s" onclick="document.getElementById('cfg-import-file').click()">&#8679; Import</button>
    </div>
    <input type="file" id="cfg-import-file" accept="application/json,.json" style="display:none" onchange="importConfigFile(this.files[0])">
    <div style="color:var(--yellow);font-size:12px;margin-top:8px">
      &#9888; Import ueberschreibt die aktuelle Konfiguration vollstaendig
      (inkl. WLAN-Zugangsdaten). Aenderungen an Port B/C/Hardware wirken wie
      ueblich erst nach einem Neustart des Moduls.
    </div>
    <!-- Zusaetzlich direkt auf/von der im Modul eingebauten SD-Karte - zwei
         eigene, explizite Aktionen (kein automatischer Export bei jedem
         Speichern), Dateiname mit fortlaufender Nummer statt echtem Datum
         (das Modul hat weder RTC noch NTP, siehe Kommentar in
         handleApiSdExport() in WebServerManager.cpp). -->
    <div style="font-size:12px;color:var(--sub);margin-top:14px;margin-bottom:10px">
      Zusaetzlich direkt auf der SD-Karte im Modul (Ordner /cfgbkup),
      unabhaengig vom Browser - z.B. um ein Ersatzmodul per Karte zu
      konfigurieren.
    </div>
    <div class="g2">
      <button class="btn btn-s" onclick="sdExportConfig()">&#8681; Auf SD sichern</button>
      <button class="btn btn-s" onclick="sdListBackups()">&#8679; Von SD laden</button>
    </div>
    <div class="hidden" id="sd-backup-list" style="margin-top:8px">
      <select id="sd-backup-select" style="width:100%;margin-bottom:6px"></select>
      <div class="g2">
        <button class="btn btn-s" onclick="sdImportSelected()">Importieren</button>
        <button class="btn btn-r" onclick="sdDeleteSelected()">L&ouml;schen</button>
      </div>
    </div>
  </div>

  <!-- Firmware-Update direkt per WLAN (OTA) - .bin-Datei hochladen, Firmware
       landet im inaktiven OTA-Segment, Modul startet danach neu. Schlaegt
       der Upload fehl/wird er abgebrochen, bootet unveraendert mit der
       bisherigen Firmware weiter (siehe handleApiOtaUpdateData() in
       WebServerManager.cpp). -->
  <div class="card" id="c-ota" style="margin-top:10px">
    <div class="card-title">Firmware-Update (OTA)</div>
    <div style="font-size:12px;color:var(--sub);margin-bottom:10px">
      Spielt eine mit PlatformIO gebaute .bin-Datei direkt per WLAN auf das
      Modul auf - Ausbauen und USB-Flashen ist damit nicht mehr noetig.
    </div>
    <button class="btn btn-s" onclick="document.getElementById('ota-file').click()">&#8679; Firmware-Datei waehlen (.bin)</button>
    <input type="file" id="ota-file" accept=".bin,application/octet-stream" style="display:none" onchange="uploadOtaFile(this.files[0])">
    <div class="row hidden" id="ota-progress" style="margin-top:8px">
      <span class="lbl" id="ota-status"></span>
    </div>
    <div style="color:var(--yellow);font-size:12px;margin-top:8px">
      &#9888; Waehrend des Uploads die Spannungsversorgung des Moduls NICHT
      unterbrechen. Update moeglichst bei ueber ESC/Akku bestromtem Modul
      durchfuehren, nicht nur ueber USB - manche USB-Anschluesse liefern
      beim Flash-Schreiben nicht genug Strom, ein dadurch ausgeloester Reset
      bricht den Upload ab. Nach erfolgreichem Upload startet das Modul
      automatisch neu und die WLAN-Verbindung bricht dabei kurz ab - das ist
      normal.
    </div>
  </div>
</div>

<!-- ═══ WIFI ════════════════════════════════════════════════════════════ -->
<div id="tab-wifi" class="hidden">
  <div class="card">
    <div class="card-title">WiFi Einstellungen</div>
    <div class="sl-wrap">
      <div class="sl-head"><span>SSID</span></div>
      <input type="text" id="w-ssid" maxlength="31">
    </div>
    <div class="sl-wrap">
      <div class="sl-head"><span>Passwort</span></div>
      <input type="password" id="w-pass" maxlength="63">
    </div>
    <div class="sl-wrap">
      <div class="sl-head"><span>AP IP-Adresse</span></div>
      <input type="text" id="w-ip" maxlength="15" placeholder="192.168.1.1">
      <div style="font-size:11px;color:var(--sub);margin-top:3px">(Neue IP gilt nach Neustart)</div>
    </div>
    <div class="sl-wrap">
      <div class="sl-head"><span>Ger&auml;tename (TBS Agent)</span></div>
      <input type="text" id="w-dev" maxlength="23">
      <div style="font-size:11px;color:var(--sub);margin-top:3px">(Im Agent sichtbar als Name@Adresse)</div>
    </div>
    <button class="btn btn-p" onclick="saveWifi()">Speichern</button>
  </div>

  <!-- "WLAN dauerhaft an" - schaltet den AP beim Booten fest ein und haelt
       ihn dauerhaft an; ueberstimmt GPIO13-Bootpin, den manuellen Schalter
       und den Auto-Failsafe weiter unten komplett (siehe config.WifiAlwaysOn
       in config.h, wifiFailsafeCheck()/setup() in ESP32-RC-Sound.ino).
       Default AN. -->
  <div class="card" id="c-wifi-always" style="margin-top:10px">
    <div class="card-title">WLAN dauerhaft aktiv (empfohlen)</div>
    <div class="row" style="margin-bottom:10px">
      <span class="lbl">Beim Start an, bleibt dauerhaft an</span>
      <input type="checkbox" id="w-always-on" onchange="onWifiAlwaysOnChange()">
    </div>
    <button class="btn btn-p" onclick="saveWifi()">Speichern</button>
    <div style="font-size:12px;color:var(--sub);margin-top:8px">
      Modul ist nach jedem Einschalten sofort per WLAN erreichbar, ohne
      GPIO13-Bootpin oder manuellen Schalter. Ist diese Option aktiv, wirken
      "WLAN automatisch bei Signalverlust" weiter unten sowie der GPIO13-
      Bootpin nicht mehr (dort ausgegraut). Eine Aenderung hier wird zwar
      sofort gespeichert, wirkt sich aber wie die anderen Einstellungen auf
      dieser Seite erst ab dem naechsten Neustart des Moduls aus - das WLAN
      bleibt also bis dahin im aktuellen Zustand.
    </div>
  </div>

  <!-- Manueller WLAN-Schalter - schaltet den AP sofort ein/aus, ohne
       Neustart und ohne das Boot-Verhalten (WifiPin/GPIO13) zu aendern
       (siehe handleApiWifiEnable() in WebServerManager.cpp). -->
  <div class="card" id="c-wifi-now" style="margin-top:10px">
    <div class="card-title">WLAN jetzt</div>
    <div class="row" style="margin-bottom:10px">
      <span class="lbl">Status</span>
      <span id="wifi-status-now">-</span>
    </div>
    <button class="btn btn-s" id="wifi-toggle-btn" onclick="toggleWifiNow()">...</button>
    <div style="font-size:12px;color:var(--sub);margin-top:8px">
      Schaltet den WLAN-Access-Point sofort ein oder aus - wirkt nur fuer die
      aktuelle Laufzeit, ohne Einfluss auf das Verhalten beim naechsten
      Einschalten des Moduls.
    </div>
  </div>

  <!-- WLAN automatisch aktivieren, wenn ueber die eingestellte Zeit kein
       gueltiges RC-Signal anliegt (siehe wifiFailsafeCheck() in
       ESP32-RC-Sound.ino) - z.B. wenn kein Sender gebunden ist. -->
  <div class="card" id="c-wifi-auto" style="margin-top:10px">
    <div class="card-title">WLAN automatisch bei Signalverlust</div>
    <div id="wifi-auto-overridden-hint" class="hidden" style="font-size:12px;color:var(--yellow);margin-bottom:8px">
      Wirkungslos, solange "WLAN dauerhaft aktiv" oben eingeschaltet ist.
    </div>
    <div class="row" style="margin-bottom:10px">
      <span class="lbl">Aktivieren</span>
      <input type="checkbox" id="w-auto-enable">
    </div>
    <div class="row">
      <span class="lbl">Timeout ohne RC-Signal (s)</span>
      <input type="number" id="w-auto-timeout" min="5" max="240" style="width:100px">
    </div>
    <button class="btn btn-p" style="margin-top:10px" onclick="saveWifi()">Speichern</button>
    <div style="font-size:12px;color:var(--sub);margin-top:8px">
      Schaltet das WLAN automatisch ein, wenn ueber die eingestellte Zeit kein
      gueltiges RC-Signal (CRSF/SBUS) anliegt. Bleibt danach an, bis es
      manuell ausgeschaltet wird oder das Modul neu startet - kein
      "Flackern", wenn das Signal zwischenzeitlich zurueckkehrt.
    </div>
  </div>
</div>

<!-- ═══ LIPO ════════════════════════════════════════════════════════════ -->
<div id="tab-lipo" class="hidden">
  <div class="card">
    <div class="card-title">Sensor Adresse (Poll-ID)</div>
    <div class="row" style="margin-bottom:4px">
      <span class="lbl">Sensor 1 (Pack 1)</span>
      <input type="text" id="pid0" maxlength="2" style="width:80px;font-family:var(--mono);text-transform:uppercase">
    </div>
    <div style="font-size:11px;color:var(--sub);margin-bottom:10px">Werkseinstellung: A1 (Physical ID 0x02)</div>
    <div class="row" style="margin-bottom:4px">
      <span class="lbl">Sensor 2 (Pack 2)</span>
      <input type="text" id="pid1" maxlength="2" style="width:80px;font-family:var(--mono);text-transform:uppercase">
    </div>
    <div style="font-size:11px;color:var(--sub);margin-bottom:10px">Werkseinstellung: 22 (Physical ID 0x03)</div>
    <button class="btn btn-p" onclick="saveLipo()">Speichern</button>
  </div>
  <div class="card">
    <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:10px">
      <span class="card-title" style="margin:0">Live Sensorwerte</span>
      <button class="btn btn-s" style="width:auto;padding:4px 12px;margin:0;font-size:11px" onclick="loadDebug()">&#8635; Aktualisieren</button>
    </div>
    <div id="lipo-packs"><div style="color:var(--sub);font-size:12px">Wird geladen...</div></div>
  </div>
</div>

<!-- ═══ DEBUG ════════════════════════════════════════════════════════════ -->
<div id="tab-debug" class="hidden">
  <div class="card">
    <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:10px">
      <span class="card-title" style="margin:0">BUS Kan&auml;le</span>
      <button class="btn btn-s" style="width:auto;padding:4px 12px;margin:0;font-size:11px" onclick="loadDebug()">&#8635; Aktualisieren</button>
    </div>
    <div class="dbg-grid" id="dbg-ch"></div>
  </div>
  <div class="card">
    <div class="card-title">PWM Pins</div>
    <div id="dbg-pwm"></div>
  </div>
  <div class="card">
    <div class="card-title">WAV Dateien</div>
    <div id="dbg-wav"></div>
  </div>
  <div class="card">
    <div class="card-title">GPS (Port C)</div>
    <div id="dbg-gps"></div>
  </div>
  <div class="card">
    <div class="card-title">S.Port Diagnose</div>
    <div id="dbg-sport-diag"></div>
  </div>
  <div class="card">
    <div class="card-title">CRSF Diagnose</div>
    <div id="dbg-crsf-diag"></div>
  </div>
  <div class="card">
    <div class="card-title">Konfiguration</div>
    <div id="dbg-cfg" class="mono" style="line-height:1.9;color:var(--sub)"></div>
  </div>
</div>

</main>
<script>
var cfg={}, dbg={}, curSnd=1, lipoTmr=0;
var TABS=['motor','sounds','settings','wifi','lipo','debug'];

// ─── Toast ────────────────────────────────────────────────────────────────
var toastTmr=0;
// Fehlercodes der /api/sound*-Endpunkte in Klartext (Begruendung siehe
// handleApiSoundUploadData() im C++-Teil).
function errText(c){
  var m={
    sd_locked:'Datei ist schreibgeschuetzt oder gesperrt - Schreibschutz auf der SD-Karte aufheben (Windows: attrib -r X:\\*.wav)',
    sd_write :'Schreiben auf die SD-Karte fehlgeschlagen (Karte voll, schreibgeschuetzt oder defekt)',
    sd_busy  :'SD-Karte gerade belegt - bitte erneut versuchen',
    bad_name :'Ungueltiger Dateiname',
    aborted  :'Uebertragung abgebrochen'
  };
  return m[c] ? m[c] : c;
}
function toast(msg,err){
  var el=document.getElementById('toast');
  el.textContent=msg; el.className=err?'err show':'show';
  clearTimeout(toastTmr);
  toastTmr=setTimeout(function(){el.className=err?'err':'';},2200);
}

// ─── Tab-Navigation ───────────────────────────────────────────────────────
function showTab(t){
  TABS.forEach(function(n){
    var el=document.getElementById('tab-'+n);
    if(el) el.className=(n===t?'':'hidden');
  });
  document.querySelectorAll('.tab').forEach(function(el,i){
    el.className='tab'+(TABS[i]===t?' active':'');
  });
  // "wifi" ist hier mit aufgenommen, damit die Karte "WLAN jetzt" den
  // aktuellen AP-/RC-Signal-Zustand zeigt (kommt ueber /api/debug, siehe
  // dbg.wifi in loadDebug()/renderWifiLiveStatus()).
  if(t==='debug'||t==='lipo'||t==='wifi') loadDebug();
  clearInterval(lipoTmr); lipoTmr=0;
  if(t==='lipo'||t==='debug'||t==='wifi') lipoTmr=setInterval(loadDebug,2000);
  // Datei-Status (vorhanden/Groesse) beim Betreten von Motor/Sounds aktuell
  // halten - z.B. wenn parallel per SD-Backup/-Karte etwas geaendert wurde.
  if(t==='motor'||t==='sounds') loadSoundFileStatus();
}

// ─── XHR-Helfer ──────────────────────────────────────────────────────────
function xget(url,_,cb){
  var x=new XMLHttpRequest();
  x.open('GET',url,true);
  x.onload=function(){if(cb)cb(x.responseText);};
  x.send();
}
function xpost(url,data,cb){
  var x=new XMLHttpRequest();
  x.open('POST',url,true);
  x.setRequestHeader('Content-Type','application/json');
  x.onload=function(){if(cb)cb(x.responseText);};
  x.onerror=function(){toast('Verbindungsfehler',1);};
  x.send(JSON.stringify(data));
}

// ─── Dropdown-Optionen ───────────────────────────────────────────────────
// Dropdown-Gruppen – aufgeteilt damit V3/V4 filtern kann
var G_BUS_LOW ={l:'BUS Kanal Low', o:function(){var a=[];for(var i=0;i<16;i++)a.push([i,'BUS Kanal Low '+(i<9?'0':'')+(i+1)]);return a;}()};
var G_BUS_HIGH={l:'BUS Kanal High',o:function(){var a=[];for(var i=0;i<16;i++)a.push([20+i,'BUS Kanal High '+(i<9?'0':'')+(i+1)]);return a;}()};
var G_PWM_LOW ={l:'PWM Pin Low',   o:function(){var a=[];for(var i=0;i<6;i++)a.push([40+i,'PWM Pin Low '+(i+1)]);return a;}()};
var G_PWM_HIGH={l:'PWM Pin High',  o:function(){var a=[];for(var i=0;i<6;i++)a.push([50+i,'PWM Pin High '+(i+1)]);return a;}()};
var G_PIN     ={l:'Eingang Pin',   o:function(){var a=[];for(var i=0;i<6;i++)a.push([60+i,'Eingang Pin '+(i+1)]);return a;}()};
var G_EK      ={l:'Einkanal',      o:function(){var a=[];for(var i=0;i<8;i++)a.push([70+i,'Einkanal '+(i<9?'0':'')+(i+1)]);return a;}()};
// "MKan" 80-95 (2 CRSF-Gruppenadressen x 8 Einzelschalter, siehe
// ESP32-RC-Sound.ino) - eine der Quellgruppen fuer Sound 1-24.
var G_MKAN    ={l:'Mehrfachkanal (MKan)', o:function(){var a=[];for(var i=0;i<16;i++)a.push([80+i,'MKan '+(i<9?'0':'')+(i+1)]);return a;}()};
var G_OPT     ={l:'Optionen',      o:[[200,'Dauerbetrieb an'],[999,'Deaktiviert']]};
var G_OPT_D   ={l:'Optionen',      o:[[999,'Deaktiviert']]};
var G_PWM_SPD ={l:'PWM Pin',       o:function(){var a=[];for(var i=0;i<6;i++)a.push([20+i,'PWM Pin '+(i+1)]);return a;}()};
var G_BUS_SPD ={l:'BUS Kanal',     o:function(){var a=[];for(var i=0;i<16;i++)a.push([i,'BUS Kanal '+(i<9?'0':'')+(i+1)]);return a;}()};

// Gruppen je nach HW-Config zusammenstellen
// hw < 2 (V1/V2): alle; hw >= 2 (V3/V4): nur BUS + Einkanal
function srcGroups(hw) {
  var g = [G_BUS_LOW, G_BUS_HIGH];
  if (hw < 2) { g.push(G_PWM_LOW, G_PWM_HIGH, G_PIN); }
  g.push(G_EK);
  g.push(G_MKAN);
  g.push(G_OPT);
  return g;
}
function spdGroups(hw) {
  var g = [G_BUS_SPD];
  if (hw < 2) g.push(G_PWM_SPD);
  g.push(G_OPT_D);
  return g;
}

function fillSel(id,groups){
  var sel=document.getElementById(id); if(!sel)return;
  sel.innerHTML='';
  groups.forEach(function(g){
    var og=document.createElement('optgroup'); og.label=g.l;
    g.o.forEach(function(o){
      var opt=document.createElement('option');
      opt.value=o[0]; opt.textContent=o[1]; og.appendChild(opt);
    });
    sel.appendChild(og);
  });
}
// id-Parameter, damit dieselbe SBUS-Kanalliste (16 Kanaele + "Deaktiviert")
// sowohl fuer "Einkanal Kanal" (ek-ch) als auch fuer die beiden SBUS-
// Gruppenkanaele (sbusgrp-ch0/1) verwendet werden kann.
function fillEkCh(id){
  var sel=document.getElementById(id); sel.innerHTML='';
  var og=document.createElement('optgroup'); og.label='SBUS Kanal';
  for(var i=0;i<16;i++){var o=document.createElement('option');o.value=i;o.textContent='SBUS Kanal '+(i<9?'0':'')+(i+1);og.appendChild(o);}
  sel.appendChild(og);
  var og2=document.createElement('optgroup'); og2.label='Optionen';
  var o2=document.createElement('option');o2.value=999;o2.textContent='Deaktiviert';og2.appendChild(o2);sel.appendChild(og2);
}
// Flache Optionsliste (Index == value), anders als fillSel() oben, das nur
// gruppierte <optgroup>-Daten kennt. Fuer Port-B-Modus genuegt eine flache
// Liste - Reihenfolge/Index kommt direkt vom Geraet (s.portb_options,
// ";"-getrennt, siehe portBFunctionOptionsString() in port_function.cpp),
// damit Web-UI und CRSF/Lua-Menue garantiert dieselbe Liste zeigen.
function fillSelFlat(id,namesStr){
  var sel=document.getElementById(id); if(!sel)return;
  sel.innerHTML='';
  if(!namesStr)return;
  namesStr.split(';').forEach(function(name,i){
    var opt=document.createElement('option');
    opt.value=i; opt.textContent=name; sel.appendChild(opt);
  });
}
function fillModAdr(){
  var sel=document.getElementById('mod-adr'); sel.innerHTML='';
  for(var i=0;i<=20;i++){var o=document.createElement('option');o.value=i;o.textContent=i;sel.appendChild(o);}
}
// Die 2 MKan-Gruppenadressen (config.EK_Gruppen_Adresse[0..1]) - wie
// mod-adr eine WM/CRSF-Adresse 0-20, zusaetzlich "Deaktiviert" (255), damit
// eine ungenutzte Gruppe bewusst abgeschaltet werden kann.
function fillGroupAdr(id){
  var sel=document.getElementById(id); sel.innerHTML='';
  for(var i=0;i<=20;i++){var o=document.createElement('option');o.value=i;o.textContent=i;sel.appendChild(o);}
  var od=document.createElement('option');od.value=255;od.textContent='Deaktiviert';sel.appendChild(od);
}

// ─── Hilfsfunktionen ────────────────────────────────────────────────────
function sv(id,v){var el=document.getElementById(id);if(el)el.value=v;}
function gv(id){var el=document.getElementById(id);return el?el.value:'';}
function gi(id){return parseInt(gv(id))||0;}
function slUpd(sid,lid){var v=gv(sid);document.getElementById(lid).textContent=v;}
function slSet(sid,lid,v){sv(sid,v);if(lid)document.getElementById(lid).textContent=v;}

// ─── RC-System / HW-Config bedingte Felder ──────────────────────────────
function onRcSys(){
  var crsf=(gi('rc-sys')===4);
  document.getElementById('c-ekch').className='card'+(crsf?' hidden':'');
  document.getElementById('c-ekmo').className='card'+(crsf?' hidden':'');
  document.getElementById('c-madr').className='card'+(crsf?'':' hidden');
  document.getElementById('c-mkan').className='card'+(crsf?'':' hidden');
  document.getElementById('c-sbusgrp').className='card'+(crsf?' hidden':'');
}
function onHwCfg(){
  var hw = gi('hw-cfg');
  document.getElementById('c-pwm').className = 'card'+(hw<2?'':' hidden');
  // Port B/C nur auf Hardware V3 sichtbar (V1/V2 brauchen GPIO32/33/27/14
  // fuer die PWM-Einkanal-Eingaenge, siehe config.h).
  var pbc=document.getElementById('c-portbc');
  if(pbc) pbc.className = 'card'+(hw>=2?'':' hidden');
  // Quell-Dropdowns neu befüllen – V3/V4 ohne PWM/Pin-Gruppen
  var curMSrc = gv('m-src');
  var curMSpd = gv('m-spd');
  var curSSrc = gv('s-src');
  fillSel('m-src', srcGroups(hw)); sv('m-src', curMSrc);
  fillSel('m-spd', spdGroups(hw));       sv('m-spd', curMSpd);
  fillSel('s-src', srcGroups(hw)); sv('s-src', curSSrc);
}
// Baudrate beim Wechsel der Port-B-Rolle auf den protokolltypischen Standard
// vorbelegen (bleibt danach frei editierbar - im Web-UI gibt es anders als
// im CRSF/Lua-Menue keine 1-Byte-Grenze, die Baudrate ist hier also
// wirklich frei eingebbar, nicht nur aus einer Liste). Die Standardwerte
// kommen vom Geraet (s.portb_baud_defaults in /api/config, siehe
// portBFunctionDefaultBaud() in port_function.cpp) statt hier als eigenes,
// von Hand synchron zu haltendes Objekt dupliziert zu sein - loadConfig()
// befuellt PORTB_BAUD_DEFAULTS unten.
var PORTB_BAUD_DEFAULTS = {};
function onPortBMode(){
  var m = gi('portb-mode');
  if (PORTB_BAUD_DEFAULTS[m] !== undefined && PORTB_BAUD_DEFAULTS[m] !== 0) sv('portb-baud', PORTB_BAUD_DEFAULTS[m]);
  var row = document.getElementById('row-portb-baud');
  if (row) row.className = 'row'+(m===0?' hidden':'');
}

// ─── Sound-Tabs ──────────────────────────────────────────────────────────
function buildSndTabs(){
  // 24 Sound-Slots.
  var c=document.getElementById('stabs'); c.innerHTML='';
  for(var i=1;i<=24;i++){
    var b=document.createElement('div');
    b.className='stab'+(i===1?' active':'');
    b.textContent='Sound '+i; b.setAttribute('data-s',i);
    b.onclick=(function(n){return function(){selSnd(n);};})(i);
    c.appendChild(b);
  }
}
function selSnd(n){
  curSnd=n;
  document.querySelectorAll('.stab').forEach(function(el){
    el.className='stab'+(parseInt(el.getAttribute('data-s'))===n?' active':'');
  });
  var lbl=document.getElementById('snd-upload-label'); if(lbl) lbl.textContent=n;
  document.getElementById('snd-upload-progress').className='row hidden';
  updateSoundFileStatusDisplay();
  if(!cfg.sounds)return;
  var s=cfg.sounds[n-1];
  sv('s-src',s.source); slSet('s-vol','lv-s-vol',s.vol); sv('s-mode',s.mode);
}

// ─── Config laden ─────────────────────────────────────────────────────────
function loadConfig(){
  xget('/api/config',0,function(resp){
    try{cfg=JSON.parse(resp);}catch(e){toast('Ladefehler',1);return;}
    document.getElementById('hv').textContent='v'+cfg.version;
    var m=cfg.motor;
    sv('m-mode',m.mode); sv('m-toggle',m.toggle);
    sv('m-src',m.source); sv('m-spd',m.speed_src);
    slSet('m-vol','lv-m-vol',m.vol);
    slSet('m-rmin','lv-m-rmin',m.rpm_min);
    slSet('m-rmax','lv-m-rmax',m.rpm_max);
    slSet('m-sdly','lv-m-sdly',m.shutdown_s);
    slSet('m-ramp','lv-m-ramp',m.ramp);
    slSet('m-dead','lv-m-dead',m.deadband);
    if(cfg.sounds) selSnd(curSnd);
    var s=cfg.settings;
    sv('rc-sys',s.rc_system); sv('ek-ch',s.ek_channel);
    sv('ek-mode',s.ek_mode); sv('mod-adr',s.modul_adr);
    sv('mkan-adr0',s.mkan_adr0); sv('mkan-adr1',s.mkan_adr1);
    sv('sbusgrp-ch0',s.sbusgrp_ch0); sv('sbusgrp-mode0',s.sbusgrp_mode0);
    sv('sbusgrp-ch1',s.sbusgrp_ch1); sv('sbusgrp-mode1',s.sbusgrp_mode1);
    sv('pwm-min',s.pwm_min); sv('pwm-max',s.pwm_max);
    sv('hw-cfg',s.hw_config_pending);
    var rh=document.getElementById('hw-restart-hint');
    if(rh) rh.className = s.restart_pending ? '' : 'hidden';
    // Port B/C laden: Options-Liste und Baudraten-Standardwerte kommen vom
    // Geraet (s.portb_options/s.portb_baud_defaults) - Dropdown wird deshalb
    // ERST befuellt, dann erst der gespeicherte Wert gesetzt.
    fillSelFlat('portb-mode', s.portb_options);
    PORTB_BAUD_DEFAULTS = s.portb_baud_defaults || {};
    sv('portb-mode', s.portb_mode); sv('portb-baud', s.portb_baud); sv('portc-gps', s.portc_gps);
    onPortBMode();
    onRcSys(); onHwCfg();
    var w=cfg.wifi;
    sv('w-ssid',w.ssid); sv('w-pass',w.pass||'');
    sv('w-ip',w.ip); sv('w-dev',w.device);
    // "WLAN dauerhaft an" (persistiert, siehe c-wifi-always) - Default AN
    var wao=document.getElementById('w-always-on');
    if(wao) wao.checked = (w.always_on===undefined) ? true : !!w.always_on;
    // WLAN-Failsafe-Einstellungen (persistiert, siehe c-wifi-auto)
    var wae=document.getElementById('w-auto-enable');
    if(wae) wae.checked = !!w.auto_enable;
    sv('w-auto-timeout', w.auto_timeout);
    onWifiAlwaysOnChange();
    var sp=cfg.sport;
    sv('pid0',sp.poll_id0); sv('pid1',sp.poll_id1);
  });
}

// ─── Debug / LiPo laden ─────────────────────────────────────────────────
function loadDebug(){
  xget('/api/debug',0,function(resp){
    try{dbg=JSON.parse(resp);}catch(e){return;}
    renderDbg(); renderLipo(); renderWifiLiveStatus();
  });
}
function renderDbg(){
  var ch=document.getElementById('dbg-ch');
  if(ch&&dbg.channels){
    ch.innerHTML='';
    dbg.channels.forEach(function(v,i){
      ch.innerHTML+='<div class="dbg-item"><div class="dbg-k">K'+(i+1)+'</div><div class="dbg-v">'+v+'</div></div>';
    });
  }
  var pw=document.getElementById('dbg-pwm');
  if(pw&&dbg.pwm){
    pw.innerHTML='';
    dbg.pwm.forEach(function(p,i){
      pw.innerHTML+='<div class="row"><span class="lbl">PWM '+(i+1)+'</span><span style="font-size:13px;font-family:var(--mono)">'+p.us+' &micro;s &rarr; '+p.pct+' %</span></div>';
    });
  }
  var wv=document.getElementById('dbg-wav');
  if(wv&&dbg.wav){
    var w=dbg.wav,h='';
    ['loop','shut','start'].forEach(function(n){
      h+='<div class="row"><span class="lbl">'+n+'.wav</span><span class="badge '+(w[n]?'bg-g':'bg-r')+'">'+(w[n]?'OK':'fehlt')+'</span></div>';
    });
    if(w.s)w.s.forEach(function(v,i){
      h+='<div class="row"><span class="lbl">sound'+(i+1)+'.wav</span><span class="badge '+(v?'bg-g':'bg-r')+'">'+(v?'OK':'fehlt')+'</span></div>';
    });
    wv.innerHTML=h;
  }
  var gp=document.getElementById('dbg-gps');
  if(gp&&dbg.gps){
    var g=dbg.gps;
    if(!g.active){gp.innerHTML='<div class="row"><span class="lbl">Status</span><span class="badge bg-r">Port C (GPS) ist aus</span></div>';}
    else if(g.pinConflict){gp.innerHTML='<div class="row"><span class="lbl">Status</span><span class="badge bg-r">deaktiviert</span></div>'+
      '<div style="font-size:11px;color:var(--sub);margin-top:8px">GPIO27-Konflikt: Port C (GPS) + SBUS gleichzeitig gewaehlt - GPIO27 wird von SBUS UND GPS beansprucht. GPS bleibt deaktiviert, SBUS hat Vorrang. Fuer GPS bitte Einkanal/RC-System auf CRSF stellen.</div>';}
    else{gp.innerHTML='<div class="row"><span class="lbl">Fix</span><span class="badge '+(g.fix?'bg-g':'bg-r')+'">'+(g.fix?'OK':'kein Fix')+'</span></div>'+
      '<div class="row"><span class="lbl">Satelliten</span><span style="font-family:var(--mono)">'+g.sats+'</span></div>'+
      '<div class="row"><span class="lbl">Geschwindigkeit</span><span style="font-family:var(--mono)">'+g.speed+' km/h</span></div>';}
  }
  // S.Port Diagnose (Learning aus ESP32-GPS-Telemetry): zeigt nicht nur
  // online/offline, sondern WARUM ein Sensor ggf. nicht online geht.
  var sd=document.getElementById('dbg-sport-diag');
  if(sd&&dbg.sport){
    var s=dbg.sport;
    var h='<div class="row"><span class="lbl">Rohbytes empfangen</span><span style="font-family:var(--mono)">'+s.raw_bytes+'</span></div>'+
      '<div class="row"><span class="lbl">Polls gesendet</span><span style="font-family:var(--mono)">'+s.polls_sent+'</span></div>'+
      '<div class="row"><span class="lbl">G&uuml;ltige Frames</span><span style="font-family:var(--mono)">'+s.valid_frames+'</span></div>'+
      '<div class="row"><span class="lbl">Keine/ung&uuml;ltige Antwort</span><span style="font-family:var(--mono)">'+s.no_response+'</span></div>'+
      '<div class="row"><span class="lbl">CRC-Fehler</span><span style="font-family:var(--mono)">'+s.crc_errors+'</span></div>'+
      '<div class="row"><span class="lbl">Letzte Poll-ID (Echo)</span><span style="font-family:var(--mono)">'+s.last_poll_id+'</span></div>';
    var hint='';
    if(s.raw_bytes==0) hint='Es kommen ueberhaupt keine Bytes an - Verkabelung/Diode/Baudrate (57600, invertiert) pruefen.';
    else if(s.valid_frames==0 && s.no_response>0) hint='Polls werden gesendet, aber es kommt nie ein gueltiges DATA_FRAME zurueck - passt die konfigurierte Physical ID (Seite 12) zu den tatsaechlich angeschlossenen FLVSS/MLVSS-Sensoren?';
    else if(s.crc_errors>s.valid_frames) hint='Viele CRC-Fehler im Verhaeltnis zu gueltigen Frames - deutet auf Stoerungen oder eine wacklige Verbindung hin.';
    if(hint) h+='<div style="font-size:11px;color:var(--sub);margin-top:8px">'+hint+'</div>';
    sd.innerHTML=h;
  }
  // CRSF Diagnose (Learning aus ESP32-GPS-Telemetry)
  var cd=document.getElementById('dbg-crsf-diag');
  if(cd&&dbg.crsf_diag){
    var c=dbg.crsf_diag;
    var h2='<div class="row"><span class="lbl">Rohbytes empfangen</span><span style="font-family:var(--mono)">'+c.raw_bytes+'</span></div>'+
      '<div class="row"><span class="lbl">G&uuml;ltige Frames</span><span style="font-family:var(--mono)">'+c.valid_frames+'</span></div>'+
      '<div class="row"><span class="lbl">CRC-Fehler</span><span style="font-family:var(--mono)">'+c.crc_errors+'</span></div>'+
      '<div class="row"><span class="lbl">Device-Pings erhalten</span><span style="font-family:var(--mono)">'+c.device_pings+'</span></div>'+
      '<div class="row"><span class="lbl">Parameter-Reads erhalten</span><span style="font-family:var(--mono)">'+c.param_reads+'</span></div>'+
      '<div class="row"><span class="lbl">Parameter-Writes erhalten</span><span style="font-family:var(--mono)">'+c.param_writes+'</span></div>';
    var hint2='';
    if(c.raw_bytes==0) hint2='Es kommen ueberhaupt keine Bytes an - TX/RX vertauscht oder RC-System nicht auf CRSF gestellt?';
    else if(c.device_pings==0) hint2='Es kommen Bytes/Frames an, aber noch kein Device-Ping vom Sender - "Sensoren entdecken"/Geraeteliste am Sender einmal ausloesen.';
    if(hint2) h2+='<div style="font-size:11px;color:var(--sub);margin-top:8px">'+hint2+'</div>';
    cd.innerHTML=h2;
  }
  var dc=document.getElementById('dbg-cfg');
  if(dc&&cfg.motor&&cfg.settings){
    var m=cfg.motor,s=cfg.settings;
    dc.innerHTML=
      'HW:'+s.hw_config+' RC-Sys:'+s.rc_system+' EK-Ch:'+s.ek_channel+'<br>'+
      'Motor-Start:'+m.source+' Speed:'+m.speed_src+'<br>'+
      'Rampe:'+m.ramp+' Totband:'+m.deadband+'<br>'+
      'S.Port:0x'+(cfg.sport?cfg.sport.poll_id0:'?')+'/0x'+(cfg.sport?cfg.sport.poll_id1:'?');
  }
}
function renderLipo(){
  var el=document.getElementById('lipo-packs');
  if(!el)return;
  if(!dbg.sport){el.innerHTML='<div style="color:var(--sub);font-size:12px">Keine Daten empfangen</div>';return;}
  if(dbg.portb_mode!==undefined&&dbg.portb_mode!==2){
    el.innerHTML='<div style="color:var(--yellow);font-size:12px">&#9888; Port B steht nicht auf S.Port-Master &ndash; LiPo-Telemetrie inaktiv.<br>Unter Einstellungen &rarr; Port B/C auf "S.Port-Master" stellen und neu starten.</div>';
    return;
  }
  var sp=dbg.sport,h='';
  sp.packs.forEach(function(p,i){
    h+='<div style="margin-bottom:14px">';
    h+='<div class="row"><span style="font-weight:700">Pack '+(i+1)+'</span>';
    h+='<span class="badge '+(p.online?'bg-g':'bg-s')+'">'+(p.online?'online':'offline')+'</span></div>';
    if(p.online){
      h+='<div class="row"><span class="lbl">Gesamt</span><span style="font-weight:700">'+p.total.toFixed(2)+' V</span></div>';
      h+='<div class="row"><span class="lbl">SoC</span><span style="font-weight:700">'+sp.soc+' %</span></div>';
      h+='<div class="row"><span class="lbl">Min-Zelle</span><span style="font-weight:700">'+p.min.toFixed(3)+' V</span></div>';
      h+='<div class="cell-grid">';
      for(var c=0;c<p.cells;c++){
        var v=p.cv[c];
        var col=v<3.5?'var(--red)':v<3.7?'var(--yellow)':'var(--green)';
        h+='<div class="cell-box"><div class="cell-v" style="color:'+col+'">'+v.toFixed(3)+'</div><div class="cell-l">Z'+(c+1)+'</div></div>';
      }
      h+='</div>';
    }else{
      h+='<div style="color:var(--sub);font-size:12px;padding:6px 0">Kein Signal &ndash; Sensor angeschlossen und Poll-ID korrekt?</div>';
    }
    h+='</div>';
  });
  el.innerHTML=h||'<div style="color:var(--sub)">Keine Sensordaten</div>';
}

// ─── Speichern ────────────────────────────────────────────────────────────
function saveMotor(){
  var m={
    source:gi('m-src'), speed_src:gi('m-spd'),
    vol:gi('m-vol'), mode:gi('m-mode'), toggle:gi('m-toggle'),
    rpm_min:gi('m-rmin'), rpm_max:gi('m-rmax'),
    shutdown_s:gi('m-sdly'), ramp:gi('m-ramp'), deadband:gi('m-dead')
  };
  xpost('/api/sound',{
    id:0, source:m.source, speed_src:m.speed_src,
    vol:m.vol, motor_mode:m.mode, toggle:m.toggle,
    rpm_min:m.rpm_min, rpm_max:m.rpm_max,
    shutdown_s:m.shutdown_s, ramp:m.ramp, deadband:m.deadband
  },function(r){
    try{var d=JSON.parse(r);if(d.ok){
      if(cfg.motor) cfg.motor=Object.assign(cfg.motor,m);
      xget('/save',0,function(){toast('Motor gespeichert');});return;}}catch(e){}
    toast('Fehler',1);
  });
}
function saveSound(){
  var newSrc=gi('s-src'), newVol=gi('s-vol'), newMode=gi('s-mode');
  xpost('/api/sound',{
    id:curSnd,
    source:newSrc, vol:newVol, mode:newMode
  },function(r){
    try{var d=JSON.parse(r);if(d.ok){
      // Lokalen cfg-Cache synchron halten, sonst zeigt ein Tab-Wechsel wieder den
      // alten (veralteten) Wert an, obwohl am Geraet bereits korrekt gespeichert ist.
      if(cfg.sounds&&cfg.sounds[curSnd-1]){
        cfg.sounds[curSnd-1].source=newSrc;
        cfg.sounds[curSnd-1].vol=newVol;
        cfg.sounds[curSnd-1].mode=newMode;
      }
      xget('/save',0,function(){toast('Sound '+curSnd+' gespeichert');});return;}}catch(e){}
    toast('Fehler',1);
  });
}
function testSound(){
  xpost('/api/sound',{id:curSnd,test:true},function(){toast('Sound '+curSnd+' wird gespielt');});
}
// WAV-Datei fuer den aktuell ausgewaehlten Sound per WLAN hochladen. Echtes
// multipart/form-data (FormData) statt einer rohen XHR-Uebertragung, damit
// die Firmware die Datei haeppchenweise verarbeiten kann, ohne sie komplett
// im RAM zu puffern - siehe handleApiSoundUploadData() in
// WebServerManager.cpp.
function uploadSoundFile(file){
  if(!file)return;
  var name='sound'+curSnd+'.wav';
  var prog=document.getElementById('snd-upload-progress');
  var status=document.getElementById('snd-upload-status');
  prog.className='row'; status.textContent='Hochladen … (0%)';

  var fd=new FormData();
  fd.append('file',file,name);

  var x=new XMLHttpRequest();
  x.open('POST','/api/soundupload?name='+encodeURIComponent(name),true);
  x.upload.onprogress=function(e){
    if(e.lengthComputable) status.textContent='Hochladen … ('+Math.round(e.loaded/e.total*100)+'%)';
  };
  x.onload=function(){
    try{
      var d=JSON.parse(x.responseText);
      if(d.ok){
        status.textContent='Fertig: '+d.file+' ('+d.size+' Bytes)';
        toast('Sound '+curSnd+' aktualisiert');
        sndFiles[name]={exists:true,size:d.size};
        updateSoundFileStatusDisplay();
      } else {
        status.textContent='Fehler: '+errText(d.error);
        toast('Upload fehlgeschlagen',1);
      }
    }catch(e){
      status.textContent='Fehler beim Hochladen';
      toast('Upload fehlgeschlagen',1);
    }
  };
  x.onerror=function(){ status.textContent='Verbindungsfehler'; toast('Verbindungsfehler',1); };
  x.send(fd);
  document.getElementById('snd-upload-file').value='';
}
// Datei-Verwaltung fuer Sound-Dateien (Motor: loop/shut/start.wav, Sound
// 1-24: soundN.wav) - Status abfragen, aktuellen Sound-Slot loeschen,
// Motor-Dateien hochladen/loeschen. sndFiles ist ein einfacher Client-Cache
// (name -> {exists,size}), damit nicht nach jeder Kleinigkeit neu vom Geraet
// gelesen werden muss; nach Upload/Loeschen wird er lokal nachgefuehrt.
var sndFiles={};
function loadSoundFileStatus(){
  xget('/api/soundfiles',0,function(r){
    try{ var d=JSON.parse(r); if(d&&d.ok&&d.files){ sndFiles=d.files; updateSoundFileStatusDisplay(); } }
    catch(e){}
  });
}
function fmtFileStatus(name){
  var f=sndFiles[name];
  if(!f) return '-';
  return f.exists ? (Math.round(f.size/1024)+' KB vorhanden') : 'keine Datei auf der SD-Karte';
}
function updateSoundFileStatusDisplay(){
  var name='sound'+curSnd+'.wav';
  var nm=document.getElementById('snd-file-name'); if(nm) nm.textContent=name;
  var st=document.getElementById('snd-file-status'); if(st) st.textContent=fmtFileStatus(name);
  ['loop','shut','start'].forEach(function(k){
    var el=document.getElementById('mf-'+k+'-status');
    if(el) el.textContent=fmtFileStatus(k+'.wav');
  });
}
function deleteSoundSlotFile(){
  var name='sound'+curSnd+'.wav';
  if(!confirm('Datei "'+name+'" von der SD-Karte loeschen?')) return;
  xpost('/api/sounddelete?name='+encodeURIComponent(name),{},function(r){
    try{
      var d=JSON.parse(r);
      if(d.ok){ toast('Sound '+curSnd+' geloescht'); sndFiles[name]={exists:false,size:0}; updateSoundFileStatusDisplay(); }
      else toast('Loeschen fehlgeschlagen: '+errText(d.error),1);
    }catch(e){ toast('Fehler',1); }
  });
}
function uploadMotorFile(name,file){
  if(!file)return;
  var prog=document.getElementById('mf-upload-progress');
  var status=document.getElementById('mf-upload-status');
  prog.className='row'; status.textContent='Hochladen '+name+' … (0%)';

  var fd=new FormData();
  fd.append('file',file,name);

  var x=new XMLHttpRequest();
  x.open('POST','/api/soundupload?name='+encodeURIComponent(name),true);
  x.upload.onprogress=function(e){
    if(e.lengthComputable) status.textContent='Hochladen '+name+' … ('+Math.round(e.loaded/e.total*100)+'%)';
  };
  x.onload=function(){
    try{
      var d=JSON.parse(x.responseText);
      if(d.ok){
        status.textContent='Fertig: '+d.file+' ('+d.size+' Bytes)';
        toast(name+' aktualisiert');
        sndFiles[name]={exists:true,size:d.size};
        updateSoundFileStatusDisplay();
      } else {
        status.textContent='Fehler: '+errText(d.error);
        toast('Upload fehlgeschlagen',1);
      }
    }catch(e){ status.textContent='Fehler beim Hochladen'; toast('Upload fehlgeschlagen',1); }
  };
  x.onerror=function(){ status.textContent='Verbindungsfehler'; toast('Verbindungsfehler',1); };
  x.send(fd);
}
function deleteMotorFile(name){
  if(!confirm('Datei "'+name+'" von der SD-Karte loeschen?')) return;
  xpost('/api/sounddelete?name='+encodeURIComponent(name),{},function(r){
    try{
      var d=JSON.parse(r);
      if(d.ok){ toast(name+' geloescht'); sndFiles[name]={exists:false,size:0}; updateSoundFileStatusDisplay(); }
      else toast('Loeschen fehlgeschlagen: '+errText(d.error),1);
    }catch(e){ toast('Fehler',1); }
  });
}
function saveSettings(){
  var d={
    rc_system:gi('rc-sys'),   ek_channel:gi('ek-ch'),
    ek_mode:gi('ek-mode'),    modul_adr:gi('mod-adr'),
    hw_config_pending:gi('hw-cfg'),
    mkan_adr0:gi('mkan-adr0'), mkan_adr1:gi('mkan-adr1'),
    sbusgrp_ch0:gi('sbusgrp-ch0'), sbusgrp_mode0:gi('sbusgrp-mode0'),
    sbusgrp_ch1:gi('sbusgrp-ch1'), sbusgrp_mode1:gi('sbusgrp-mode1'),
    portb_mode:gi('portb-mode'), portb_baud:gi('portb-baud'), portc_gps:gi('portc-gps')
  };
  // PWM nur bei V1/V2 senden (bei V3/V4 nicht vorhanden)
  if(gi('hw-cfg')<2){d.pwm_min=gi('pwm-min');d.pwm_max=gi('pwm-max');}
  xpost('/api/config',d,function(r){
    // Kein zusaetzliches /save noetig - handleApiConfigPost() markiert die
    // Konfiguration serverseitig als "dirty", der eigentliche NVS-Schreib-
    // vorgang folgt automatisch ueber die debounced+cooldown-geschuetzte
    // Pruefung in loop() (seit v7.16, siehe dortigen Kommentar) - genau wie
    // bei /api/sound.
    try{var p=JSON.parse(r);if(p.ok){
      // Komplett neu von /api/config laden statt nur die gerade gesendeten
      // Felder lokal zu uebernehmen: "restart_pending" wird vom Server
      // berechnet (nicht Teil von d) und muss deshalb aktualisiert werden,
      // damit der Neustart-Hinweis sofort stimmt, ohne dass man die Seite
      // neu laden muss.
      loadConfig();
      toast('Einstellungen gespeichert');return;}}catch(e){}
    toast('Fehler',1);
  });
}

// ─── Konfiguration Export/Import ─────────────────────────────────────────
// Export: liest /api/config (dieselbe JSON-Struktur, die auch loadConfig()
// nutzt) und bietet sie als Datei zum Download an - ganz normaler Download
// im echten Browser des Nutzers (dieser Webserver laeuft auf dem Modul
// selbst, keine Sandbox-Einschraenkung wie in einer Vorschau-Umgebung).
function exportConfig(){
  xget('/api/config',0,function(resp){
    var text=resp;
    try{ text=JSON.stringify(JSON.parse(resp),null,2); }catch(e){ /* notfalls Rohtext exportieren */ }
    var name='esp32-rc-sound-config';
    var dev=gv('w-dev');
    if(dev) name+='-'+dev.replace(/[^A-Za-z0-9_-]+/g,'_');
    name+='.json';
    var blob=new Blob([text],{type:'application/json'});
    var url=URL.createObjectURL(blob);
    var a=document.createElement('a');
    a.href=url; a.download=name;
    document.body.appendChild(a); a.click(); document.body.removeChild(a);
    setTimeout(function(){URL.revokeObjectURL(url);},1000);
    toast('Konfiguration exportiert');
  });
}

// Import: Datei einlesen, grob validieren, bestaetigen lassen (WLAN-Zugangs-
// daten werden mit ueberschrieben), dann Schritt fuer Schritt einspielen.
function importConfigFile(file){
  if(!file) return;
  var reader=new FileReader();
  reader.onload=function(){
    var data;
    try{ data=JSON.parse(reader.result); }catch(e){ toast('Ungueltige JSON-Datei',1); return; }
    if(!data||!data.settings||!data.motor||!data.sounds){
      toast('Datei enthaelt keine gueltige Modul-Konfiguration',1); return;
    }
    if(!confirm('Aktuelle Konfiguration (inkl. WLAN-Zugangsdaten) durch die Datei ersetzen?')) return;
    importConfigData(data);
  };
  reader.onerror=function(){ toast('Datei konnte nicht gelesen werden',1); };
  reader.readAsText(file);
  // Sonst laesst sich dieselbe Datei kein zweites Mal auswaehlen (onchange
  // feuert bei unveraendertem value nicht erneut).
  document.getElementById('cfg-import-file').value='';
}

function stripUndef(o){
  Object.keys(o).forEach(function(k){ if(o[k]===undefined) delete o[k]; });
  return o;
}
function runSteps(steps,done){
  var i=0;
  function nextStep(){
    if(i>=steps.length){ done(); return; }
    steps[i++](nextStep);
  }
  nextStep();
}

// /api/config und /api/sound sind bewusst einfache Key/Value-Endpunkte ohne
// Array-Unterstuetzung (siehe handleApiConfigPost()/handleApiSound() in
// WebServerManager.cpp) - der Import laeuft daher als Kette einzelner POSTs:
// einmal /api/config fuer die globalen Einstellungen, einmal /api/sound fuer
// Motor (id 0), dann /api/sound je Sound-Slot, zum Schluss ein einzelnes
// /save (statt 25x einzeln auf den Flash zu schreiben).
function importConfigData(data){
  var s=data.settings||{}, w=data.wifi||{}, sp=data.sport||{}, m=data.motor||{};

  var settingsPost=stripUndef({
    rc_system:s.rc_system, ek_channel:s.ek_channel, ek_mode:s.ek_mode, modul_adr:s.modul_adr,
    mkan_adr0:s.mkan_adr0, mkan_adr1:s.mkan_adr1,
    sbusgrp_ch0:s.sbusgrp_ch0, sbusgrp_mode0:s.sbusgrp_mode0,
    sbusgrp_ch1:s.sbusgrp_ch1, sbusgrp_mode1:s.sbusgrp_mode1,
    hw_config_pending:s.hw_config_pending, pwm_min:s.pwm_min, pwm_max:s.pwm_max,
    portb_mode:s.portb_mode, portb_baud:s.portb_baud, portc_gps:s.portc_gps,
    ssid:w.ssid, pass:w.pass, ip:w.ip, device:w.device,
    poll_id0:sp.poll_id0, poll_id1:sp.poll_id1
  });

  // Feldname-Stolperfalle: /api/config liefert Motor-throttle_mode als
  // "mode" (siehe handleApiConfig()), /api/sound erwartet dafuer aber
  // "motor_mode" - "mode" ist dort bereits fuer Mode_Sound[0] belegt (im
  // Motor-Tab der Weboberflaeche gar nicht angezeigt). Deshalb hier bewusst
  // NICHT 1:1 durchgereicht, sondern umbenannt.
  var motorPost=stripUndef({
    id:0, source:m.source, vol:m.vol, speed_src:m.speed_src,
    motor_mode:m.mode, toggle:m.toggle,
    rpm_min:m.rpm_min, rpm_max:m.rpm_max, shutdown_s:m.shutdown_s,
    ramp:m.ramp, deadband:m.deadband
  });

  var soundPosts=(data.sounds||[]).map(function(sd){
    return stripUndef({ id:sd.id, source:sd.source, vol:sd.vol, mode:sd.mode });
  });

  var steps=[
    function(next){ xpost('/api/config', settingsPost, function(){ next(); }); },
    function(next){ xpost('/api/sound', motorPost, function(){ next(); }); }
  ];
  soundPosts.forEach(function(p){
    if(p.id===undefined) return;
    steps.push(function(next){ xpost('/api/sound', p, function(){ next(); }); });
  });
  steps.push(function(next){ xget('/save',0,function(){ next(); }); });

  toast('Import laeuft...');
  runSteps(steps,function(){
    loadConfig();
    toast('Konfiguration importiert');
  });
}

// ─── Konfiguration Export/Import direkt auf/von der SD-Karte im Modul ─────
// Eigene, explizite Buttons zusaetzlich zum Browser-Export/-Import oben.
// Der Import nutzt bewusst dieselbe importConfigData(), die auch der
// Browser-Import benutzt (Datei liegt bereits im /api/config-Format vor).
function sdExportConfig(){
  xpost('/api/sdexport',{},function(r){
    var p; try{ p=JSON.parse(r); }catch(e){ toast('Fehler beim Sichern auf SD',1); return; }
    if(p&&p.ok){ toast('Auf SD gesichert: '+p.file); return; }
    toast('Fehler beim Sichern auf SD'+(p&&p.error?' ('+p.error+')':''),1);
  });
}
function sdListBackups(){
  xget('/api/sdlist',0,function(r){
    var p; try{ p=JSON.parse(r); }catch(e){ toast('Fehler beim Lesen der SD-Karte',1); return; }
    if(!p||!p.ok||!p.files||!p.files.length){ toast('Keine Backups auf der SD-Karte gefunden',1); return; }
    p.files.sort(function(a,b){ return a.name<b.name?1:(a.name>b.name?-1:0); }); // neuestes (hoechste Nummer) zuerst
    var sel=document.getElementById('sd-backup-select');
    sel.innerHTML='';
    p.files.forEach(function(f){
      var o=document.createElement('option');
      o.value=f.name; o.textContent=f.name+' ('+f.size+' Byte)';
      sel.appendChild(o);
    });
    document.getElementById('sd-backup-list').className='';
  });
}
function sdImportSelected(){
  var name=document.getElementById('sd-backup-select').value;
  if(!name) return;
  xget('/api/sdconfig?file='+encodeURIComponent(name),0,function(r){
    var data; try{ data=JSON.parse(r); }catch(e){ toast('Ungueltige Datei auf der SD-Karte',1); return; }
    if(!data||!data.settings||!data.motor||!data.sounds){
      toast('Datei enthaelt keine gueltige Modul-Konfiguration',1); return;
    }
    if(!confirm('Aktuelle Konfiguration (inkl. WLAN-Zugangsdaten) durch "'+name+'" ersetzen?')) return;
    importConfigData(data);
  });
}
// Ausgewaehltes Config-Backup von der SD-Karte loeschen (Teil der
// Datei-Verwaltung) - danach Liste neu laden, damit sie aktuell bleibt.
function sdDeleteSelected(){
  var name=document.getElementById('sd-backup-select').value;
  if(!name) return;
  if(!confirm('Backup "'+name+'" von der SD-Karte loeschen?')) return;
  xpost('/api/sddelete?file='+encodeURIComponent(name),{},function(r){
    var d; try{ d=JSON.parse(r); }catch(e){ toast('Fehler',1); return; }
    if(d.ok){ toast('Backup geloescht'); sdListBackups(); }
    else{ toast('Loeschen fehlgeschlagen: '+errText(d.error),1); }
  });
}

// Firmware-Update per WLAN (OTA) - echtes multipart/form-data wie beim
// Sound-Upload (uploadSoundFile()), aber an /api/otaupdate; nach Erfolg
// startet das Modul selbst neu (siehe handleApiOtaUpdate() in
// WebServerManager.cpp) - die Verbindung bricht dabei kurz ab, das wird dem
// Nutzer hier erklaert statt als Fehler angezeigt.
function uploadOtaFile(file){
  if(!file)return;
  if(!confirm('Firmware-Update mit "'+file.name+'" starten? Waehrend des '+
              'Uploads die Spannungsversorgung des Moduls NICHT unterbrechen '+
              '- moeglichst ueber ESC/Akku bestromt, nicht nur ueber USB. '+
              'Das Modul startet danach automatisch neu.')){
    document.getElementById('ota-file').value=''; return;
  }
  var prog=document.getElementById('ota-progress');
  var status=document.getElementById('ota-status');
  prog.className='row'; status.textContent='Hochladen … (0%)';

  var fd=new FormData();
  fd.append('file',file,file.name);

  var x=new XMLHttpRequest();
  x.open('POST','/api/otaupdate',true);
  x.upload.onprogress=function(e){
    if(e.lengthComputable) status.textContent='Hochladen … ('+Math.round(e.loaded/e.total*100)+'%)';
  };
  x.onload=function(){
    // Bei Erfolg startet das Modul (nach kurzer Verzoegerung serverseitig)
    // von selbst neu - die Antwort trifft trotzdem noch ein, bevor die
    // Verbindung tatsaechlich abbricht.
    try{
      var d=JSON.parse(x.responseText);
      if(d.ok){
        status.textContent='Update erfolgreich ('+d.size+' Bytes) - Modul startet neu …';
        toast('Firmware-Update erfolgreich, Modul startet neu');
      } else {
        status.textContent='Fehler: '+errText(d.error);
        toast('Update fehlgeschlagen',1);
      }
    }catch(e){ status.textContent='Fehler beim Hochladen'; toast('Update fehlgeschlagen',1); }
  };
  x.onerror=function(){
    // Kann auch bedeuten, dass das Update erfolgreich war und das Modul
    // bereits neu startet, bevor die Antwort ankam - fuer den Nutzer nicht
    // unterscheidbar von einem echten Verbindungsfehler.
    status.textContent='Verbindung unterbrochen - Update war evtl. trotzdem erfolgreich (Modul neu verbinden und pruefen)';
  };
  x.send(fd);
  document.getElementById('ota-file').value='';
}

// Graut die Auto-Failsafe-Karte aus, solange "WLAN dauerhaft an" aktiv ist -
// diese Einstellungen wirken dann nicht (siehe wifiFailsafeCheck()).
function onWifiAlwaysOnChange(){
  var always = document.getElementById('w-always-on');
  var on = always ? always.checked : false;
  var hint = document.getElementById('wifi-auto-overridden-hint');
  if(hint) hint.className = on ? '' : 'hidden';
  ['w-auto-enable','w-auto-timeout'].forEach(function(id){
    var el=document.getElementById(id);
    if(el){ el.disabled=on; el.style.opacity = on ? '0.5' : '1'; }
  });
}
function saveWifi(){
  var d={ ssid:gv('w-ssid'), pass:gv('w-pass'), ip:gv('w-ip'), device:gv('w-dev'),
          wifi_always_on: document.getElementById('w-always-on').checked?1:0,
          wifi_auto_enable: document.getElementById('w-auto-enable').checked?1:0,
          wifi_auto_timeout: gi('w-auto-timeout') };
  xpost('/api/config',d,function(r){
    try{var p=JSON.parse(r);if(p.ok){
      if(cfg.wifi) cfg.wifi=Object.assign(cfg.wifi,{ssid:d.ssid,pass:d.pass,ip:d.ip,device:d.device,
        always_on:d.wifi_always_on,auto_enable:d.wifi_auto_enable,auto_timeout:d.wifi_auto_timeout});
      toast('WiFi gespeichert');return;}}catch(e){}
    toast('Fehler',1);
  });
}
// Manueller WLAN-Schalter (Karte "WLAN jetzt") - schaltet ueber
// /api/wifi_enable sofort um, ohne die Konfiguration zu beruehren.
function toggleWifiNow(){
  var turnOn = !(dbg.wifi && dbg.wifi.active);
  xpost('/api/wifi_enable',{on:turnOn},function(r){
    try{
      var d=JSON.parse(r);
      dbg.wifi = dbg.wifi || {}; dbg.wifi.active = d.active;
      renderWifiLiveStatus();
      toast(d.active?'WLAN eingeschaltet':'WLAN ausgeschaltet');
    }catch(e){ toast('Fehler',1); }
  });
}
// Zeigt den aktuellen (Laufzeit-)WLAN-Zustand aus dbg.wifi (kommt ueber
// /api/debug, siehe loadDebug() - wird beim Betreten des Tabs "WiFi" und
// danach alle 2s aktualisiert, siehe showTab()).
function renderWifiLiveStatus(){
  var active = !!(dbg.wifi && dbg.wifi.active);
  var st=document.getElementById('wifi-status-now');
  var btn=document.getElementById('wifi-toggle-btn');
  if(st) st.textContent = active ? 'Ein' : 'Aus';
  if(btn) btn.textContent = active ? 'WLAN jetzt ausschalten' : 'WLAN jetzt einschalten';
}
function saveLipo(){
  var d={ poll_id0:gv('pid0'), poll_id1:gv('pid1') };
  xpost('/api/config',d,function(r){
    try{var p=JSON.parse(r);if(p.ok){
      if(cfg.sport) cfg.sport=Object.assign(cfg.sport,d);
      toast('S.Port gespeichert');return;}}catch(e){}
    toast('Fehler',1);
  });
}

// ─── Init ────────────────────────────────────────────────────────────────
// Dropdowns initial mit hw=0 befüllen; nach loadConfig() mit echtem hw-Wert neu befüllen
fillSel('m-src', srcGroups(0, false));
fillSel('m-spd', spdGroups(0));
fillSel('s-src', srcGroups(0));
fillEkCh('ek-ch'); fillModAdr(); fillGroupAdr('mkan-adr0'); fillGroupAdr('mkan-adr1');
fillEkCh('sbusgrp-ch0'); fillEkCh('sbusgrp-ch1'); buildSndTabs();
loadConfig();
loadSoundFileStatus(); // Datei-Status initial laden (Tab "Motor" ist beim Start aktiv)
</script>
</body>
</html>)HTML";


WebServer     WebServerManager::server(80);
int           WebServerManager::Menu             = 0;
String        WebServerManager::valueString      = "";
unsigned long WebServerManager::lastSdActivityMs = 0;

// v7.16: Cooldown-Dauer nach einer SD-Schreiboperation, bevor der naechste
// blockierende NVS-Flash-Schreibvorgang (saveConfigForce(), siehe loop() in
// ESP32-RC-Sound.ino) zugelassen wird. Feldbefund: ohne Stuetzkondensator am
// Modul (Versorgung ueber lange, duenne SBUS-Leitung vom Empfaenger-BEC)
// reichte ein SD-Upload direkt gefolgt von einem sofortigen Config-Save aus,
// um einen Brownout-Reset-Loop auszuloesen; ein paar Sekunden Abstand (in
// der Praxis durch Umschalten zwischen Sound-Slots erzeugt) behoben es
// zuverlaessig. 3000ms bildet das nach.
static constexpr unsigned long SD_SAVE_COOLDOWN_MS = 3000;

void WebServerManager::markSdActivity() { lastSdActivityMs = millis(); }

bool WebServerManager::sdActivityCooldownActive() {
  return lastSdActivityMs != 0 && (millis() - lastSdActivityMs) < SD_SAVE_COOLDOWN_MS;
}

extern bool          Sound_on_web[RCSOUND_NUM_SLOTS]; // v5: 25 statt 9
extern uint16_t      channel_output[16];
extern volatile unsigned int PWM_pulse_width[6];
extern XT_Wav_Class  Sound_loop;
extern XT_Wav_Class  Sound_shut;
extern XT_Wav_Class  Sound_start;
// Pointer-Array Sounds[] (Index 0=Motor/ungenutzt, 1..24=Sound1..Sound24,
// siehe ESP32-RC-Sound.ino) - deckt alle RCSOUND_NUM_SLOTS-1 Sounds ab.
extern XT_Wav_Class* Sounds[RCSOUND_NUM_SLOTS];
extern char          versionString[6];
extern uint16_t      Version; // fuer den Dateinamen beim SD-Export, siehe handleApiSdExport()
extern bool          isValidEinSource(int c); // siehe ESP32-RC-Sound.ino
extern bool          BUS_OK; // fuer den WLAN-Live-Status in handleApiDebug(), siehe unten

// storedApSsid/-Password merken sich die Zugangsdaten aus begin() fuer
// einen spaeteren enableAP()-Aufruf (WLAN kann jederzeit zur Laufzeit
// gestartet werden, nicht nur beim Booten), apActive haelt den aktuellen
// (Laufzeit-)Zustand fest.
static char storedApSsid[32]     = "";
static char storedApPassword[64] = "";
static bool apActive             = false;

// URL-Dekodierung
String WebServerManager::urlDecode(const String& s) {
  String result = "";
  result.reserve(s.length());
  for (int i = 0; i < (int)s.length(); i++) {
    if (s[i] == '+') { result += ' '; }
    else if (s[i] == '%' && i+2 < (int)s.length()) {
      char hex[3] = { s[i+1], s[i+2], 0 };
      result += (char)strtol(hex, nullptr, 16);
      i += 2;
    } else { result += s[i]; }
  }
  return result;
}

// ── Routen registrieren - IMMER aus setup() aufgerufen, unabhaengig vom ──
// WifiPin-Zustand: das eigentliche Ein-/Ausschalten von AP-Funk und
// Webserver-Socket macht enableAP()/disableAP() weiter unten, getrennt
// davon. Registrierung der Routen selbst ist billig (nur Lambda-Zeiger in
// einer internen Liste), unabhaengig vom WLAN-Zustand jederzeit unkritisch.
void WebServerManager::begin(const char* apSsid, const char* apPassword) {
  strncpy(storedApSsid,     apSsid,     sizeof(storedApSsid)-1);     storedApSsid[sizeof(storedApSsid)-1]=0;
  strncpy(storedApPassword, apPassword, sizeof(storedApPassword)-1); storedApPassword[sizeof(storedApPassword)-1]=0;

  // Alle Requests auf einen Handler
  server.onNotFound([]() { WebServerManager::handleRequest(); });
  server.on("/", []()    { WebServerManager::handleRequest(); });
  server.on("/sport",         []() { WebServerManager::handleSport();     });
  server.on("/api/config", HTTP_GET,  []() { WebServerManager::handleApiConfig(); });
  server.on("/api/config", HTTP_POST, []() { WebServerManager::handleApiConfigPost(); });
  server.on("/api/debug",     []() { WebServerManager::handleApiDebug();  });
  server.on("/api/sound",     []() { WebServerManager::handleApiSound();  });
  // Konfiguration direkt auf/von der SD-Karte sichern/laden
  server.on("/api/sdexport", HTTP_POST, []() { WebServerManager::handleApiSdExport(); });
  server.on("/api/sdlist",   HTTP_GET,  []() { WebServerManager::handleApiSdList();   });
  server.on("/api/sdconfig", HTTP_GET,  []() { WebServerManager::handleApiSdConfig(); });
  // Sound-WAV per WLAN hochladen - zweiter Handler-Parameter ist der
  // eigentliche Upload-Callback (mehrfach pro Request, siehe HTTPUpload-
  // Status in handleApiSoundUploadData()); der erste wird einmal am Ende
  // aufgerufen und schickt die JSON-Antwort.
  server.on("/api/soundupload", HTTP_POST,
            []() { WebServerManager::handleApiSoundUpload(); },
            []() { WebServerManager::handleApiSoundUploadData(); });
  // Datei-Verwaltung - Status aller Sound-Dateien, gezieltes Loeschen einer
  // Sound-Datei bzw. eines Config-Backups.
  server.on("/api/soundfiles",  HTTP_GET,  []() { WebServerManager::handleApiSoundFiles(); });
  server.on("/api/sounddelete", HTTP_POST, []() { WebServerManager::handleApiSoundDelete(); });
  server.on("/api/sddelete",    HTTP_POST, []() { WebServerManager::handleApiSdDelete();    });
  // Firmware-Update per WLAN (OTA) - gleiches Zwei-Handler-Muster wie
  // /api/soundupload oben, schreibt aber in das OTA-Flash-Segment statt auf
  // die SD-Karte (siehe handleApiOtaUpdateData()).
  server.on("/api/otaupdate", HTTP_POST,
            []() { WebServerManager::handleApiOtaUpdate(); },
            []() { WebServerManager::handleApiOtaUpdateData(); });
  // Manueller WLAN-Schalter (Web-Tab "WiFi"/Lua-Feld 174).
  server.on("/api/wifi_enable", HTTP_POST, []() { WebServerManager::handleApiWifiEnable(); });
  // Ein fehlgeschlagener AP-Start (z.B. WiFi-Treiber/Heap zum Zeitpunkt des
  // Aufrufs noch nicht bereit) ist im Serial-Log sichtbar (siehe enableAP()
  // weiter unten, wo softAP() tatsaechlich aufgerufen wird) - server.on()-
  // Routen muessen trotzdem hier, in begin(), einmalig registriert bleiben.
}

// ── AP tatsaechlich aktivieren (WiFi-Funk + Webserver-Socket) ────────────
// Aufrufbar sowohl beim Booten als auch zur Laufzeit: manuell (Web-Button/
// Lua-Feld 174) oder automatisch bei RC-Signalverlust (siehe
// wifiFailsafeCheck() in ESP32-RC-Sound.ino). Idempotent - ein bereits
// aktiver AP wird nicht doppelt gestartet. Der Freie-Heap-Log gilt fuer
// JEDEN AP-Start, nicht nur den beim Booten: WiFi.softAP() ist in diesem
// Projekt historisch die stoeranfaelligste Stelle ueberhaupt - ein AP-Start
// spaet im Betrieb (nach den Sound-Puffern, evtl. zusaetzlich fragmentiert
// durch Sound-Uploads) laeuft auf einem deutlich unguenstigeren Heap als
// der sorgfaeltig VOR AllocateSoundBuffers() platzierte Boot-Start - dieser
// Log macht das im Fehlerfall sichtbar.
void WebServerManager::enableAP() {
  if (apActive) return;
  Serial.printf("Freier Heap vor WLAN-Start: %u Bytes\n", (unsigned)ESP.getFreeHeap());
  bool apOk = WiFi.softAP(storedApSsid, storedApPassword);
  if (!apOk) Serial.println("!!! FEHLER: WiFi.softAP() fehlgeschlagen !!!");
  IPAddress apIP, gw, subnet(255,255,255,0);
  if (!apIP.fromString(config.WiFi_IP)) apIP.fromString("192.168.1.1");
  gw = apIP;
  WiFi.softAPConfig(apIP, gw, subnet);
  server.begin();
  apActive = true;
  Serial.print("AP gestartet, IP: ");
  Serial.println(WiFi.softAPIP());
}

// ── AP wieder abschalten ──────────────────────────────────────────────────
// Schliesst den Webserver-Socket und schaltet den WLAN-Funk komplett ab
// (softAPdisconnect(true), nicht nur die Verbindungen trennen). Idempotent.
void WebServerManager::disableAP() {
  if (!apActive) return;
  server.stop();
  WiFi.softAPdisconnect(true);
  apActive = false;
  Serial.println("AP gestoppt.");
}

bool WebServerManager::isApActive() { return apActive; }
// handleApiWifiEnable() (POST /api/wifi_enable) steht weiter unten, in der
// Naehe von handleApiOtaUpdate() - braucht die dort bereits verfuegbaren
// Helfer sendJson()/sendOk() (in dieser Datei erst spaeter definiert).

// ── handleClient(): non-blocking, kehrt sofort zurueck ──────────────────
void WebServerManager::Webpage() {
  server.handleClient();
}

// ── /sport JSON-Endpunkt (LiPo Live-Daten) ──────────────────────────────
void WebServerManager::handleSport() {
  char json[512]; int pos = 0;
  pos += snprintf(json+pos, sizeof(json)-pos, "{");
  for (uint8_t i = 0; i < 2; i++) {
    pos += snprintf(json+pos, sizeof(json)-pos,
      "\"s%u\":{\"online\":%s,\"cells\":%u,\"total\":%.2f,\"min\":%.3f,\"soc\":%u,\"cv\":[",
      i, lipoSensor[i].online ? "true" : "false",
      lipoSensor[i].cellCount, lipoSensor[i].totalVoltage,
      lipoSensor[i].minCell, sportCalcSoC(lipoSensor[i].minCell));
    for (uint8_t c = 0; c < 6; c++)
      pos += snprintf(json+pos, sizeof(json)-pos, "%.3f%s",
        lipoSensor[i].cellVoltage[c], c < 5 ? "," : "");
    pos += snprintf(json+pos, sizeof(json)-pos, "]}%s", i < 1 ? "," : "");
  }
  pos += snprintf(json+pos, sizeof(json)-pos,
    ",\"pid0\":\"%02X\",\"pid1\":\"%02X\"}",
    config.sport_poll_id[0], config.sport_poll_id[1]);
  server.send(200, "application/json", json);
}


// ── JSON-Hilfsfunktionen ──────────────────────────────────────────────────
static void sendJson(WebServer& sv, const String& json) {
  sv.sendHeader("Cache-Control", "no-cache");
  sv.send(200, "application/json", json);
}
static void sendOk(WebServer& sv)  { sendJson(sv, "{\"ok\":true}"); }
static void sendErr(WebServer& sv) { sv.send(400, "application/json", "{\"error\":\"bad request\"}"); }

// jsonGetInt/jsonGetStr sind keine echte JSON-Bibliothek (das waere ein
// groesserer Umbau mit Hardware-Testbedarf), aber der Schluessel muss an
// einer Wortgrenze stehen (davor kein Namens-/Zahlenzeichen), sonst wuerde
// z.B. der Schluessel "mode" faelschlich mitten in "motor_mode" matchen.
// Optionaler Leerraum zwischen ":" und dem Wert wird toleriert.
static bool jsonFindKey(const String& body, const char* key, int& valueStart) {
  String k = "\"" + String(key) + "\"";
  int searchFrom = 0;
  for (;;) {
    int pos = body.indexOf(k, searchFrom);
    if (pos < 0) return false;
    // Zeichen VOR dem Schluessel darf kein Namensbestandteil eines laengeren
    // Schluessels sein (z.B. verhindert das "mode" in "motor_mode" zu matchen,
    // wenn direkt davor ein Buchstabe/Ziffer/_ steht statt eines Trennzeichens).
    bool boundaryOk = (pos == 0) || !(isalnum((unsigned char)body[pos-1]) || body[pos-1]=='_');
    int afterKey = pos + k.length();
    int colon = afterKey;
    while (colon < (int)body.length() && isspace((unsigned char)body[colon])) colon++;
    if (boundaryOk && colon < (int)body.length() && body[colon] == ':') {
      valueStart = colon + 1;
      while (valueStart < (int)body.length() && isspace((unsigned char)body[valueStart])) valueStart++;
      return true;
    }
    searchFrom = pos + 1; // Kandidat verworfen, weitersuchen (z.B. war es Teil eines laengeren Keys)
  }
}
static bool jsonGetInt(const String& body, const char* key, int& out) {
  int vs;
  if (!jsonFindKey(body, key, vs)) return false;
  out = body.substring(vs).toInt();
  return true;
}
static bool jsonGetStr(const String& body, const char* key, char* out, size_t maxlen) {
  int vs;
  if (!jsonFindKey(body, key, vs)) return false;
  if (vs >= (int)body.length() || body[vs] != '"') return false;
  vs++; // öffnendes Anführungszeichen überspringen
  int end = body.indexOf('"', vs);
  if (end < 0) return false;
  String val = body.substring(vs, end);
  strncpy(out, val.c_str(), maxlen - 1);
  out[maxlen - 1] = '\0';
  return true;
}

// JSON-Sonderzeichen in Ausgabewerten escapen (SSID, Passwort, Geraetename
// werden sonst direkt konkateniert - ein Anfuehrungszeichen oder Backslash
// darin erzeugt sonst ungueltiges JSON, das der Browser-Parser ablehnt).
static String jsonEscape(const char* s) {
  String out;
  for (const char* p = s; *p; ++p) {
    switch (*p) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if ((uint8_t)*p < 0x20) { /* sonstige Steuerzeichen ueberspringen */ }
        else out += *p;
    }
  }
  return out;
}


// ── POST /api/config ─────────────────────────────────────────────────────
// Schreibt Settings, WiFi und S.Port Poll-IDs.
// Body: JSON mit beliebigen der folgenden Felder (alle optional):
//   settings: { rc_system, ek_channel, ek_mode, modul_adr,
//               mkan_adr0, mkan_adr1,
//               sbusgrp_ch0, sbusgrp_mode0, sbusgrp_ch1, sbusgrp_mode1,
//               hw_config, pwm_min, pwm_max }
//   wifi:     { ssid, pass, ip, device }
//   sport:    { poll_id0, poll_id1 }  (Hex-String, z.B. "A1")
void WebServerManager::handleApiConfigPost() {
  if (!server.hasArg("plain")) { sendErr(server); return; }
  const String& body = server.arg("plain");
  bool changed = false;
  int  v;

  // Settings
  if (jsonGetInt(body, "rc_system",  v)) { config.Einkanal_RC_System        = constrain(v,0,4);    changed=true; }
  if (jsonGetInt(body, "ek_channel", v)) { config.Einkanal_Channel           = (v==999)?999:constrain(v,0,15); changed=true; }
  if (jsonGetInt(body, "ek_mode",    v)) { config.Einkanal_mode              = v;                   changed=true; }
  if (jsonGetInt(body, "modul_adr",  v)) { config.modul_adress               = constrain(v,0,20);   changed=true; }
  // Die unabhaengigen MKan-Gruppenadressen - jede fuer sich auf 0-255
  // begrenzt (255=Gruppe deaktiviert), wie auch im CRSF-Parametermenue
  // (siehe crsfWriteParam() idx 168-169 in ESP32-RC-Sound.ino). Duerfen sich
  // unterscheiden oder gleich sein - hier keine Reihenfolge/Eindeutigkeit
  // erzwungen (siehe Kommentar in config.h/EK_Gruppen_Adresse).
  if (jsonGetInt(body, "mkan_adr0", v)) { config.EK_Gruppen_Adresse[0]       = constrain(v,0,255);  changed=true; }
  if (jsonGetInt(body, "mkan_adr1", v)) { config.EK_Gruppen_Adresse[1]       = constrain(v,0,255);  changed=true; }
  // SBUS-Pendant zu mkan_adr0/1 (siehe config.h/SBUS_Gruppen_Channel): je
  // ein fest zugewiesener SBUS-Kanal (999=Gruppe aus) statt einer freien
  // Busadresse, plus Modus auf diesem Kanal - dieselbe Konvention wie
  // ek_mode: 0=Normal, 10-13=WM Adr 0-3 (kein constrain(0,3), siehe
  // Kommentar bei config.SBUS_Gruppen_Mode/config.Einkanal_mode).
  if (jsonGetInt(body, "sbusgrp_ch0",   v)) { config.SBUS_Gruppen_Channel[0] = (v==999)?999:constrain(v,0,15); changed=true; }
  if (jsonGetInt(body, "sbusgrp_mode0", v)) { config.SBUS_Gruppen_Mode[0]    = v;                    changed=true; }
  if (jsonGetInt(body, "sbusgrp_ch1",   v)) { config.SBUS_Gruppen_Channel[1] = (v==999)?999:constrain(v,0,15); changed=true; }
  if (jsonGetInt(body, "sbusgrp_mode1", v)) { config.SBUS_Gruppen_Mode[1]    = v;                    changed=true; }
  if (jsonGetInt(body, "hw_config_pending",  v)) { config.Hardware_Config_Pending = constrain(v,0,2); changed=true;
    Serial.printf("[API] Hardware_Config_Pending -> %d (wirkt erst nach Neustart)\n", config.Hardware_Config_Pending); }
  if (jsonGetInt(body, "pwm_min",    v)) { config.PWM_scale_min              = constrain(v,0,4095); changed=true; }
  if (jsonGetInt(body, "pwm_max",    v)) { config.PWM_scale_max              = constrain(v,0,4095); changed=true; }
  // Port B / Port C - wirkt wie hw_config_pending erst nach einem Neustart
  // (siehe setup() in ESP32-RC-Sound.ino, PortB_Mode_boot/
  // PortC_GPS_Enabled_boot). Anders als im CRSF/Lua-Menue (1-Byte-
  // Wertebereich, siehe dort) ist die Baudrate hier ueber JSON wirklich
  // frei eingebbar, kein Snapping auf eine feste Liste.
  if (jsonGetInt(body, "portb_mode", v)) { config.PortB_Mode = constrain(v,0,(int)portBFunctionMaxIndex()); changed=true;
    Serial.printf("[API] Port B Modus -> %d (wirkt erst nach Neustart)\n", config.PortB_Mode); }
  if (jsonGetInt(body, "portb_baud", v)) { config.PortB_Baud = (uint32_t)constrain(v,300,2000000); changed=true; }
  if (jsonGetInt(body, "portc_gps",  v)) { config.PortC_GPS_Enabled = constrain(v,0,1); changed=true;
    Serial.printf("[API] Port C (GPS) -> %d (wirkt erst nach Neustart)\n", config.PortC_GPS_Enabled); }

  // WiFi
  { char tmp[64];
    if (jsonGetStr(body, "ssid",   config.WiFi_SSID,     sizeof(config.WiFi_SSID)))     changed=true;
    if (jsonGetStr(body, "pass",   config.WiFi_Password, sizeof(config.WiFi_Password))) changed=true;
    if (jsonGetStr(body, "ip",     config.WiFi_IP,       sizeof(config.WiFi_IP)))       changed=true;
    if (jsonGetStr(body, "device", config.Device_Name,   sizeof(config.Device_Name)))   changed=true;
    (void)tmp;
  }
  // WLAN-Failsafe-Einstellungen (der manuelle "WLAN jetzt"-Schalter laeuft
  // NICHT hierueber, sondern ueber /api/wifi_enable).
  if (jsonGetInt(body, "wifi_auto_enable",  v)) { config.WifiAutoEnable     = constrain(v,0,1) ? true : false; changed=true; }
  if (jsonGetInt(body, "wifi_auto_timeout", v)) { config.WifiAutoTimeoutSec = (uint16_t)constrain(v,5,240);    changed=true; }
  // "WLAN dauerhaft an" (siehe config.h) - rein persistierte Einstellung wie
  // 175/176 (Auto-Failsafe) oder portb_mode/portc_gps oben: wirkt erst ab
  // dem naechsten Bootvorgang, KEIN sofortiges enableAP() hier. Bewusst so
  // (seit v7.15) - ein WLAN-Start zur Laufzeit ist historisch die
  // fehleranfaelligste Operation in diesem Projekt (Heap-Fragmentierung,
  // siehe SDCardInit()-Kommentar in ESP32-RC-Sound.ino); ein Aufruf aus
  // diesem HTTP-Handler heraus ist dafuer unnoetiges zusaetzliches Risiko,
  // da der AP durch den Default "an" beim Booten ohnehin schon laeuft.
  if (jsonGetInt(body, "wifi_always_on", v)) {
      config.WifiAlwaysOn = constrain(v,0,1) ? true : false;
      changed = true;
  }

  // S.Port Poll-IDs (Hex-String "A1" → uint8_t 0xA1)
  { char tmp[8];
    if (jsonGetStr(body, "poll_id0", tmp, sizeof(tmp)))
      { config.sport_poll_id[0]=(uint8_t)strtol(tmp,nullptr,16); changed=true; }
    if (jsonGetStr(body, "poll_id1", tmp, sizeof(tmp)))
      { config.sport_poll_id[1]=(uint8_t)strtol(tmp,nullptr,16); changed=true; }
  }

  // v7.16: nur noch markDirty() statt eines sofortigen, blockierenden
  // saveConfigForce() hier im HTTP-Handler - der eigentliche NVS-Schreib-
  // vorgang passiert jetzt ausschliesslich ueber die debounced Pruefung in
  // loop() (ESP32-RC-Sound.ino), die zusaetzlich einen SD-Aktivitaets-
  // Cooldown beachtet (siehe sdActivityCooldownActive()). Aendert nichts an
  // der Persistenz (spaetestens ~500ms-3.5s spaeter gespeichert), reduziert
  // aber das Risiko eines Brownouts, wenn kurz zuvor auf die SD-Karte
  // geschrieben wurde.
  if (changed) { markDirty(); }
  sendOk(server);
}

// ── GET /api/config ───────────────────────────────────────────────────────
// Alle Konfigurationswerte als JSON.
// Felder: motor{}, sounds[], settings{}, sport{}, wifi{}, version
// Eigene Funktion (nicht Teil von handleApiConfig()), damit derselbe JSON-
// String auch fuer den SD-Export (siehe handleApiSdExport() weiter unten)
// genutzt werden kann, statt ihn dort ein zweites Mal von Hand
// zusammenzubauen (und damit doppelt pflegen zu muessen).
String WebServerManager::buildConfigJson() {
  String j = "{";

  // Motor (Sound 0)
  j += "\"motor\":{";
  j += "\"source\":"     + String(config.Source_Start_Sound[0]) + ",";
  j += "\"speed_src\":"  + String(config.Source_Speed_Sound_0)  + ",";
  j += "\"vol\":"        + String(config.Volumen_Sound[0])       + ",";
  j += "\"mode\":"       + String(config.throttle_mode)          + ",";
  j += "\"toggle\":"     + String(config.engine_on_toggle)       + ",";
  j += "\"rpm_min\":"    + String(config.Min_Speed_Sound_0)      + ",";
  j += "\"rpm_max\":"    + String(config.Max_Speed_Sound_0)      + ",";
  j += "\"shutdown_s\":" + String(config.shutdowndelay)          + ",";
  j += "\"ramp\":"       + String(config.throttle_ramp)          + ",";
  j += "\"deadband\":"   + String(config.throttle_dead_band)     + "},";

  // Sounds 1-24, deckt RCSOUND_NUM_SLOTS-1 vollstaendig ab
  j += "\"sounds\":[";
  for (int i = 1; i < RCSOUND_NUM_SLOTS; i++) {
    if (i > 1) j += ",";
    j += "{\"id\":"     + String(i)                            + ",";
    j += "\"source\":"  + String(config.Source_Start_Sound[i]) + ",";
    j += "\"vol\":"     + String(config.Volumen_Sound[i])       + ",";
    j += "\"mode\":"    + String(config.Mode_Sound[i])          + "}";
  }
  j += "],";

  // Einstellungen
  j += "\"settings\":{";
  j += "\"hw_config\":"  + String(config.Hardware_Config)         + ",";
  j += "\"hw_config_pending\":" + String(config.Hardware_Config_Pending) + ",";
  j += "\"restart_pending\":" + String(hardwareConfigRestartPending() ? 1 : 0) + ",";
  j += "\"portb_mode\":" + String(config.PortB_Mode) + ",";
  j += "\"portb_baud\":" + String(config.PortB_Baud) + ",";
  // Optionsliste + Standard-Baudraten kommen aus der Port-B-Rollen-Registry
  // (port_function.h/.cpp), statt sie hier, im HTML und im JS zusaetzlich
  // von Hand identisch zu pflegen. Index in beiden Listen == numerischer
  // PortBMode-Wert (siehe fillSelFlat()/onPortBMode() im Web-UI-JavaScript
  // unten).
  j += "\"portb_options\":\"" + String(portBFunctionOptionsString()) + "\",";
  j += "\"portb_baud_defaults\":[";
  for (uint8_t i = 0; i < PORTB_FUNCTION_COUNT; i++) {
    if (i > 0) j += ",";
    j += String(portBFunctionDefaultBaud((PortBMode)i));
  }
  j += "],";
  j += "\"portc_gps\":"  + String(config.PortC_GPS_Enabled) + ",";
  j += "\"rc_system\":"  + String(config.Einkanal_RC_System)      + ",";
  j += "\"ek_channel\":" + String(config.Einkanal_Channel)        + ",";
  j += "\"ek_mode\":"    + String(config.Einkanal_mode)           + ",";
  j += "\"modul_adr\":"  + String(config.modul_adress)            + ",";
  // Die MKan-Gruppenadressen (auch ueber das CRSF-Parametermenue am Sender
  // einstellbar).
  j += "\"mkan_adr0\":"  + String(config.EK_Gruppen_Adresse[0])   + ",";
  j += "\"mkan_adr1\":"  + String(config.EK_Gruppen_Adresse[1])   + ",";
  // SBUS-Pendant zu mkan_adr0/1 (siehe config.h/SBUS_Gruppen_Channel).
  j += "\"sbusgrp_ch0\":"   + String(config.SBUS_Gruppen_Channel[0]) + ",";
  j += "\"sbusgrp_mode0\":" + String(config.SBUS_Gruppen_Mode[0])    + ",";
  j += "\"sbusgrp_ch1\":"   + String(config.SBUS_Gruppen_Channel[1]) + ",";
  j += "\"sbusgrp_mode1\":" + String(config.SBUS_Gruppen_Mode[1])    + ",";
  j += "\"pwm_min\":"    + String(config.PWM_scale_min)           + ",";
  j += "\"pwm_max\":"    + String(config.PWM_scale_max)           + "},";

  // S.Port Poll-IDs
  char p0[4], p1[4];
  snprintf(p0, sizeof(p0), "%02X", config.sport_poll_id[0]);
  snprintf(p1, sizeof(p1), "%02X", config.sport_poll_id[1]);
  j += "\"sport\":{\"poll_id0\":\"" + String(p0) + "\",\"poll_id1\":\"" + String(p1) + "\"},";

  // WiFi
  j += "\"wifi\":{";
  j += "\"pass\":\""   + jsonEscape(config.WiFi_Password) + "\",";
  j += "\"ssid\":\""   + jsonEscape(config.WiFi_SSID)     + "\",";
  j += "\"ip\":\""     + jsonEscape(config.WiFi_IP)     + "\",";
  j += "\"device\":\"" + jsonEscape(config.Device_Name) + "\",";
  // WLAN-Failsafe-Einstellungen (persistiert) - der aktuelle Live-Zustand
  // (gerade an/aus, RC-Signal da/weg) steht bewusst NICHT hier, sondern in
  // /api/debug (haeufiger abgefragt, siehe handleApiDebug()).
  j += "\"auto_enable\":"  + String(config.WifiAutoEnable ? 1 : 0)   + ",";
  j += "\"auto_timeout\":" + String(config.WifiAutoTimeoutSec)      + ",";
  j += "\"always_on\":"    + String(config.WifiAlwaysOn ? 1 : 0)    + "},";

  j += "\"version\":\"" + String(versionString) + "\"";
  j += "}";
  return j;
}

void WebServerManager::handleApiConfig() {
  sendJson(server, buildConfigJson());
}

// ── SD-Kartenpfad fuer die Konfigurations-Backups ─────────────────────────
// Eigener Unterordner statt Wurzelverzeichnis, um nicht mit den Sound-Dateien
// (/sound1.wav ... /sound24.wav, /loop.wav, /shut.wav, /start.wav, siehe
// ESP32-RC-Sound.ino) zu kollidieren. Kurzer Name ohne Punkt fuer maximale
// Kompatibilitaet (8.3-sicher), auch wenn aktuelle SD-Bibliotheken lange
// Dateinamen normalerweise unterstuetzen.
static constexpr const char* SD_BACKUP_DIR = "/cfgbkup";

// Erlaubt fuer Dateinamen, die per /api/sdconfig?file=... angefragt werden:
// nur das Muster, das handleApiSdExport() selbst erzeugt. Verhindert Pfad-
// Traversal (z.B. "../"), ohne dass eine echte Pfad-Bibliothek noetig ist.
static bool isSafeBackupFileName(const String& name) {
  if (name.length() == 0 || name.length() > 40) return false;
  if (name.indexOf('/') >= 0 || name.indexOf("..") >= 0) return false;
  for (size_t i = 0; i < name.length(); i++) {
    char c = name[i];
    bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') || c == '_' || c == '.' || c == '-';
    if (!ok) return false;
  }
  return true;
}

// ── POST /api/sdexport ─────────────────────────────────────────────────────
// Schreibt dieselbe JSON-Konfiguration, die auch /api/config liefert, direkt
// als neue Datei auf die SD-Karte (explizite Aktion per Button im Webinter-
// face, kein automatischer Export). Der Dateiname enthaelt eine fortlaufende,
// in NVS gespeicherte Nummer (siehe nextSdBackupCounter() in config.cpp) statt
// eines echten Zeitstempels - das Modul hat weder eine batteriegepufferte RTC
// noch (im reinen AP-Betrieb ohne Internet) eine NTP-Zeitquelle; ein GPS-Datum
// waere nur verfuegbar, wenn Port C als GPS aktiv ist UND ein Fix vorliegt.
void WebServerManager::handleApiSdExport() {
  if (!sdMutexTake(3000)) {
    server.send(503, "application/json", "{\"ok\":false,\"error\":\"sd_busy\"}");
    return;
  }
  if (!SD.exists(SD_BACKUP_DIR)) SD.mkdir(SD_BACKUP_DIR);

  uint32_t n = nextSdBackupCounter();
  char fname[48];
  snprintf(fname, sizeof(fname), "%s/config_%04u_v%u.json", SD_BACKUP_DIR, (unsigned)n, (unsigned)Version);

  String j = buildConfigJson();
  bool ok = false;
  File f = SD.open(fname, FILE_WRITE);
  if (f) {
    ok = (f.print(j) == (int)j.length());
    f.close();
  }
  sdMutexGive();

  if (ok) {
    sendJson(server, "{\"ok\":true,\"file\":\"" + String(fname + 1) + "\"}"); // ohne fuehrenden "/"
  } else {
    server.send(500, "application/json", "{\"ok\":false,\"error\":\"sd_write\"}");
  }
}

// ── GET /api/sdlist ─────────────────────────────────────────────────────────
// Liste der bisherigen Backup-Dateien im Ordner SD_BACKUP_DIR, fuer die
// Auswahl im Webinterface vor einem Import.
void WebServerManager::handleApiSdList() {
  String j = "{\"ok\":true,\"files\":[";
  bool first = true;

  if (sdMutexTake(3000)) {
    if (SD.exists(SD_BACKUP_DIR)) {
      File dir = SD.open(SD_BACKUP_DIR);
      if (dir && dir.isDirectory()) {
        File entry = dir.openNextFile();
        while (entry) {
          if (!entry.isDirectory()) {
            String nm = String(entry.name());
            int slash = nm.lastIndexOf('/');
            if (slash >= 0) nm = nm.substring(slash + 1);
            if (nm.endsWith(".json") && isSafeBackupFileName(nm)) {
              if (!first) j += ",";
              j += "{\"name\":\"" + nm + "\",\"size\":" + String(entry.size()) + "}";
              first = false;
            }
          }
          entry.close();
          entry = dir.openNextFile();
        }
        dir.close();
      }
    }
    sdMutexGive();
  }

  j += "]}";
  sendJson(server, j);
}

// ── GET /api/sdconfig?file=... ──────────────────────────────────────────────
// Liest eine einzelne Backup-Datei ein und liefert ihren Inhalt unveraendert
// zurueck - das ist bereits dieselbe JSON-Struktur wie /api/config, damit
// kann das Webinterface sie direkt an die vorhandene importConfigData()
// uebergeben (siehe JavaScript unten), ohne die Import-Logik ein zweites Mal
// zu implementieren.
void WebServerManager::handleApiSdConfig() {
  String name = server.arg("file");
  if (!isSafeBackupFileName(name)) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad_name\"}");
    return;
  }
  String path = String(SD_BACKUP_DIR) + "/" + name;

  if (!sdMutexTake(3000)) {
    server.send(503, "application/json", "{\"ok\":false,\"error\":\"sd_busy\"}");
    return;
  }
  File f = SD.open(path, FILE_READ);
  if (!f) {
    sdMutexGive();
    server.send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
    return;
  }
  String content = f.readString();
  f.close();
  sdMutexGive();

  sendJson(server, content);
}

// ── POST /api/sddelete?file=... ──────────────────────────────────────────
// Loescht ein einzelnes Konfigurations-Backup aus SD_BACKUP_DIR (Teil der
// Datei-Verwaltung im Webinterface, Tab "Einstellungen") - dieselbe
// Namenspruefung wie /api/sdconfig, verhindert Pfad-Traversal.
void WebServerManager::handleApiSdDelete() {
  String name = server.arg("file");
  if (!isSafeBackupFileName(name)) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad_name\"}");
    return;
  }
  String path = String(SD_BACKUP_DIR) + "/" + name;

  if (!sdMutexTake(3000)) {
    server.send(503, "application/json", "{\"ok\":false,\"error\":\"sd_busy\"}");
    return;
  }
  bool ok = SD.remove(path);
  sdMutexGive();
  // v7.16: siehe Kommentar bei markSdActivity().
  markSdActivity();

  if (ok) {
    sendJson(server, "{\"ok\":true,\"file\":\"" + name + "\"}");
  } else {
    server.send(500, "application/json", "{\"ok\":false,\"error\":\"sd_delete\"}");
  }
}

// ── POST /api/soundupload?name=... ───────────────────────────────────────
// Sound-WAV-Dateien direkt per WLAN auf die SD-Karte hochladen - erspart
// das Ausbauen der SD-Karte fuer neue/geaenderte Sounds. Nur die von der
// Firmware tatsaechlich erwarteten Dateinamen werden akzeptiert (kein
// beliebiges Schreiben auf die SD-Karte ueber diesen Endpunkt).
static bool isValidSoundUploadName(const String& name) {
  if (name == "loop.wav" || name == "shut.wav" || name == "start.wav") return true;
  // "sound1.wav" .. "sound24.wav"
  if (!name.startsWith("sound") || !name.endsWith(".wav")) return false;
  String numPart = name.substring(5, name.length() - 4);
  if (numPart.length() == 0 || numPart.length() > 2) return false;
  for (size_t i = 0; i < numPart.length(); i++) {
    if (!isDigit(numPart[i])) return false;
  }
  int n = numPart.toInt();
  return n >= 1 && n <= 24;
}

// Liefert (falls vorhanden) das Sound-Objekt, das die Datei "name" abspielt -
// Bugfix v7.09: siehe Aufrufer (handleApiSoundUploadData()/-Delete()). Fuer
// Sound1-24 (Sounds[]) wird das Handle vom Haupt-Loop() ohnehin bei jedem
// neuen Trigger frisch per LoadWavFile() geoeffnet (siehe handleSound() im
// .ino); fuer die drei Motor-Dateien (loop/shut/start.wav) dagegen NICHT -
// die werden per LoadFiles() nur EINMAL beim Boot geoeffnet und danach nie
// wieder angefasst, solange der Motor laeuft. Das Handle bleibt also die
// gesamte Laufzeit ueber offen.
static XT_Wav_Class* soundObjectFor(const String& name) {
  if (name == "loop.wav")  return &Sound_loop;
  if (name == "shut.wav")  return &Sound_shut;
  if (name == "start.wav") return &Sound_start;
  if (name.startsWith("sound") && name.endsWith(".wav")) {
    int n = name.substring(5, name.length() - 4).toInt();
    if (n >= 1 && n <= 24) return Sounds[n];
  }
  return nullptr;
}

// ── v7.11: Datei-Deskriptoren waehrend Upload/Loeschen voruebergehend freigeben
// Der ESP32 erlaubt nur max_files gleichzeitig offene Dateien auf der SD-Karte
// (vfs_fat, Standardwert 5 - siehe SDCardInit() im .ino, wo ausfuehrlich steht,
// warum dieser Wert NICHT erhoeht werden darf: max_files=15 hat in v7.07..v7.10
// reproduzierbar den WLAN-Start zerstoert).
// Von diesen 5 Deskriptoren sind 3 permanent durch die Motor-Dateien
// (loop/shut/start.wav) belegt - LoadFiles() oeffnet sie EINMAL beim Boot und
// schliesst sie nie wieder, auch wenn der Motor gar nicht laeuft. Zusammen mit
// jedem gerade aktiven Sound1-24 blieb fuer den Upload oft kein einziger
// Deskriptor mehr uebrig -> SD.open() scheiterte mit "no free file descriptors"
// und es traf je nach zufaellig gerade offenen Sounds mal Slot 1, mal 3, mal 8.
// Loesung ohne jeden zusaetzlichen Speicherbedarf: die NICHT gerade
// abspielenden Motor-Handles fuer die Dauer des Uploads schliessen und danach
// wieder laden. Ein aktuell abspielender Motor-Sound wird bewusst in Ruhe
// gelassen (Playing-Abfrage) - im Normalfall laeuft er ohnehin nicht, weil der
// AP per Failsafe nur ohne gueltiges RC-Signal hochkommt.
// WICHTIG: UnLoadWavFile()/LoadWavFile() nehmen den (nicht rekursiven)
// SD-Mutex selbst - beide Funktionen duerfen deshalb NUR AUSSERHALB eines
// eigenen sdMutexTake()/sdMutexGive()-Fensters aufgerufen werden (sonst
// Deadlock), genau wie beim Ziel-Sound in den Aufrufern weiter unten.
static XT_Wav_Class* const kMotorSounds[3] = { &Sound_loop, &Sound_shut, &Sound_start };

// Schliesst die ungenutzten Motor-Handles; "except" (der Sound, den der
// Aufrufer ohnehin selbst schliesst/laedt) wird uebersprungen.
// Rueckgabe: Bitmaske der tatsaechlich geschlossenen Handles fuer restore...().
// v7.12: deckt jetzt NICHT nur die 3 Motor-Dateien ab, sondern auch die 24
// Sound-Slots. Grund: handleSound() im .ino schliesst das Handle eines Slots
// erst, wenn dessen Quelle wieder AUS geht UND die Wiedergabe beendet ist -
// ein Slot, dessen Quelle dauerhaft anliegt (z.B. Quelle "immer an", oder ein
// Schalter, der angeschaltet stehen bleibt), belegt seinen Deskriptor also
// dauerhaft weiter, obwohl gar nichts mehr zu hoeren ist. Genau diese Slots
// haben den knappen Vorrat zusaetzlich zu den Motor-Dateien aufgebraucht.
// Bit 0-2 = Motor (loop/shut/start), Bit 3-26 = Sounds[1..24].
static uint32_t releaseIdleSoundHandles(XT_Wav_Class* except) {
  uint32_t mask = 0;
  for (uint8_t i = 0; i < 3; i++) {
    XT_Wav_Class* s = kMotorSounds[i];
    if (s == except)  continue;  // behandelt der Aufrufer selbst
    if (s->Playing)   continue;  // laeuft gerade - Handle in Ruhe lassen
    if (!s->WavFile)  continue;  // war gar nicht offen - siehe WICHTIG unten
    s->UnLoadWavFile();
    mask |= (1ul << i);
  }
  for (uint8_t n = 1; n <= 24; n++) {
    XT_Wav_Class* s = Sounds[n];
    if (s == nullptr) continue;
    if (s == except)  continue;
    if (s->Playing)   continue;  // laufende Wiedergabe nicht unterbrechen
    if (!s->WavFile)  continue;  // war gar nicht offen - siehe WICHTIG unten
    s->UnLoadWavFile();
    mask |= (1ul << (n + 2));
  }
  return mask;
}

// Gegenstueck: laedt GENAU die Handles wieder, die oben geschlossen wurden.
// WICHTIG: Die Maske darf ausschliesslich Handles enthalten, die vorher
// tatsaechlich offen waren (deshalb die WavFile-Abfrage oben). Wuerde hier ein
// Slot geladen, der vorher zu war, haette der Upload am Ende MEHR offene
// Dateien hinterlassen als vorher - bei nur 5 verfuegbaren Deskriptoren waere
// das sofort der naechste "no free file descriptors"-Fehler, und zwar als
// Dauerzustand.
static void restoreSoundHandles(uint32_t mask) {
  for (uint8_t i = 0; i < 3; i++)
    if (mask & (1ul << i)) kMotorSounds[i]->LoadWavFile();
  for (uint8_t n = 1; n <= 24; n++)
    if ((mask & (1ul << (n + 2))) && Sounds[n]) Sounds[n]->LoadWavFile();
}

// ── v7.12: Diagnose fuer fehlgeschlagene Sound-Uploads ───────────────────
// Bei einem gescheiterten Upload stand bisher nur ein Fehlercode in der
// Weboberflaeche ("sd_write"), im Serial-Log aber nichts Verwertbares. Diese
// Zeile macht die drei plausiblen Ursachen auf einen Blick unterscheidbar:
//   - Karte voll          -> frei(Bytes) sehr klein bzw. < Dateigroesse
//   - Deskriptor-Engpass  -> motor=<Maske> zeigt, wieviele Handles frei waren
//   - Speicher/sonstiges  -> heap
static void logSdUploadFail(const char* name, const char* err,
                            uint32_t motorMask, uint32_t bytesSoFar) {
  uint64_t total = 0, used = 0;
  // SD.totalBytes()/usedBytes() koennen bei nicht gemounteter Karte 0 liefern -
  // genau das ist dann ebenfalls eine verwertbare Aussage.
  total = SD.totalBytes();
  used  = SD.usedBytes();
  Serial.printf("[SD-Upload] FEHLER %s bei /%s | geschrieben=%u B | "
                "Karte: %llu frei von %llu B | freigegebene Handles=0x%08X | Heap=%u\n",
                err, name, (unsigned)bytesSoFar,
                (unsigned long long)(total > used ? total - used : 0),
                (unsigned long long)total, motorMask,
                (unsigned)ESP.getFreeHeap());
}

// Zustand waehrend eines laufenden Uploads (ein Upload zur Zeit - der ESP32-
// WebServer bearbeitet Requests ohnehin nacheinander, nicht parallel).
static File   uploadFile;
static bool   uploadMutexHeld = false;
static bool   uploadFailed    = false;
static String uploadErrCode;
static uint32_t uploadBytes   = 0;
// v7.09: das Sound-Objekt, dessen Datei GERADE hochgeladen wird (siehe
// soundObjectFor()) - am START gemerkt, damit es am ENDE (nach erfolgreichem
// Schreiben) mit der neuen Datei neu geladen werden kann, statt bis zum
// naechsten Trigger/Reboot mit geschlossenem Handle dazustehen.
static XT_Wav_Class* uploadTargetSound = nullptr;
// v7.11: Bitmaske der fuer die Dauer des Uploads freigegebenen Motor-Handles
// (siehe releaseIdleSoundHandles()) - am Ende wieder herzustellen.
static uint32_t uploadMotorMask = 0;

// Eigentlicher Upload-Callback: wird vom ESP32-WebServer fuer JEDES Haeppchen
// der multipart/form-data-Uebertragung erneut aufgerufen (upload.status
// durchlaeuft START -> mehrfach WRITE -> END, oder ABORTED bei Abbruch). Die
// Datei wird dabei haeppchenweise direkt auf die SD-Karte geschrieben, NIE
// komplett im RAM gepuffert (WAV-Dateien koennen mehrere hundert KB gross
// sein - das wuerde den ~300 KB Heap des ESP32 sonst leicht sprengen).
// Der SD-Mutex wird schon bei START genommen und erst bei END/ABORTED wieder
// freigegeben - also fuer die GESAMTE Upload-Dauer, nicht nur pro Haeppchen -
// sonst koennte die Sound-Wiedergabe mitten in eine gerade unvollstaendige
// Datei hineinlesen.
void WebServerManager::handleApiSoundUploadData() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    uploadFailed = false; uploadErrCode = ""; uploadBytes = 0; uploadMutexHeld = false;
    uploadTargetSound = nullptr;
    uploadMotorMask = 0;

    String name = server.arg("name");
    if (!isValidSoundUploadName(name)) {
      uploadFailed = true; uploadErrCode = "bad_name";
      return;
    }

    // v7.09 Bugfix: ist die Datei GERADE aktiv (z.B. Sound im Loop-Modus
    // waehrend er laeuft, oder eine der drei Motor-Dateien - deren Handle ist
    // seit LoadFiles() beim Boot ohnehin dauerhaft offen), haelt die Audio-
    // Engine ihr WavFile-Handle noch offen. FILE_WRITE haengt bei der
    // Arduino-SD-Bibliothek an eine bereits vorhandene Datei AN statt sie zu
    // ueberschreiben (O_APPEND); das anschliessende SD.remove() sollte das
    // verhindern, schlaegt aber auf eine noch geoeffnete Datei haeufig fehl -
    // und der Rueckgabewert wurde bisher NICHT geprueft. Ergebnis: der
    // Upload haengte sich stillschweigend HINTEN an die alte Datei an, statt
    // sie zu ersetzen (meldete trotzdem "ok", die Datei begann aber
    // weiterhin mit dem alten Sound) - Ursache fuer "Sound X laesst sich
    // nicht ueberschreiben", waehrend andere, gerade nicht aktive Slots
    // problemlos funktionierten.
    // Fix: das Sound-Objekt hier VORHER aktiv schliessen, UND den Erfolg von
    // SD.remove() pruefen statt ihn zu ignorieren.
    // WICHTIG: UnLoadWavFile()/LoadWavFile() nehmen den SD-Mutex JEWEILS
    // SELBST (siehe XT_I2S_Audio.cpp) - er ist NICHT rekursiv, ein Aufruf
    // waehrend wir ihn selbst schon halten wuerde also sofort blockieren
    // (Deadlock). Deshalb hier schliessen, BEVOR wir unten sdMutexTake()
    // fuer den eigentlichen Schreibvorgang nehmen.
    XT_Wav_Class* target = soundObjectFor(name);
    if (target) target->UnLoadWavFile();
    // v7.11: zusaetzlich die ungenutzten Motor-Handles freigeben, damit fuer
    // SD.open() unten garantiert ein Deskriptor frei ist (siehe
    // releaseIdleSoundHandles()). Muss - wie das Schliessen des Ziel-Sounds
    // direkt darueber - VOR sdMutexTake() passieren (Deadlock-Gefahr).
    uint32_t motorMask = releaseIdleSoundHandles(target);

    if (!sdMutexTake(3000)) {
      uploadFailed = true; uploadErrCode = "sd_busy";
      logSdUploadFail(name.c_str(), "sd_busy", motorMask, 0);
      if (target) target->LoadWavFile(); // vorherigen Zustand wiederherstellen
      restoreSoundHandles(motorMask);
      return;
    }
    uploadMutexHeld = true;
    uploadTargetSound = target;
    uploadMotorMask   = motorMask;

    SD.remove("/" + name);
    if (SD.exists("/" + name)) {
      // Datei liess sich trotz Schliessversuch nicht entfernen - lieber
      // sauber mit Fehler abbrechen als stillschweigend an die alte Datei
      // anzuhaengen.
      // BESTAETIGTE Hauptursache (Feldanalyse v7.12): die Datei traegt auf der
      // SD-Karte das FAT-Attribut "schreibgeschuetzt" (AM_RDO). FatFs lehnt
      // f_unlink() darauf mit FR_DENIED ab, und auch ein Oeffnen mit
      // FILE_WRITE scheitert - Loeschen UND Ueberschreiben sind also blockiert.
      // Windows uebernimmt dieses Attribut beim Kopieren auf die Karte oft
      // unbemerkt mit, und zwar nur fuer einzelne Dateien. Genau daher das
      // lange verwirrende Bild "Sound 1 und 3 gehen nicht, 5-7 schon" und das
      // Wandern des Fehlers nach einem Dateitausch per Kartenleser.
      // Abhilfe am PC: attrib -r X:\*.wav
      // Die Firmware kann das Attribut ueber das Arduino-SD-API NICHT selbst
      // zuruecksetzen (kein f_chmod() exponiert) - deshalb hier bewusst nur
      // ein sauberer, klar benannter Abbruch statt eines stillen Fehlers.
      uploadFailed = true; uploadErrCode = "sd_locked";
      logSdUploadFail(name.c_str(), "sd_locked (Datei schreibgeschuetzt?)", motorMask, 0);
      sdMutexGive(); uploadMutexHeld = false;
      uploadTargetSound = nullptr; uploadMotorMask = 0;
      if (target) target->LoadWavFile(); // Mutex ist jetzt frei - alte Datei besteht ja weiter
      restoreSoundHandles(motorMask);
      return;
    }
    uploadFile = SD.open("/" + name, FILE_WRITE);
    if (!uploadFile) {
      uploadFailed = true; uploadErrCode = "sd_write";
      logSdUploadFail(name.c_str(), "sd_write (SD.open fehlgeschlagen)", motorMask, 0);
      sdMutexGive(); uploadMutexHeld = false;
      uploadTargetSound = nullptr; uploadMotorMask = 0;
      // Datei existiert nach dem entfernten Original nicht mehr - nichts
      // Sinnvolles zum Neuladen da, target bleibt bewusst leer (FileOK=false).
      restoreSoundHandles(motorMask);
      return;
    }
    Serial.printf("[SD-Upload] Start: /%s\n", name.c_str());

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFailed || !uploadFile) return; // vorheriger Fehler - Rest verwerfen
    size_t written = uploadFile.write(upload.buf, upload.currentSize);
    uploadBytes += written;
    if (written != upload.currentSize) {
      uploadFailed = true; uploadErrCode = "sd_write"; // z.B. SD-Karte voll
      // Teilschreibvorgang - typischstes Zeichen fuer eine volle Karte.
      logSdUploadFail(server.arg("name").c_str(), "sd_write (Teilschreibvorgang)",
                      uploadMotorMask, uploadBytes);
    }

  } else if (upload.status == UPLOAD_FILE_END || upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();
    if (upload.status == UPLOAD_FILE_ABORTED) { uploadFailed = true; uploadErrCode = "aborted"; }
    if (uploadFailed) {
      // Unvollstaendige/fehlerhafte Datei nicht auf der Karte stehen lassen.
      String name = server.arg("name");
      if (isValidSoundUploadName(name)) SD.remove("/" + name);
    } else {
      Serial.printf("[SD-Upload] Fertig: %u Bytes\n", (unsigned)uploadBytes);
    }
    XT_Wav_Class* target = uploadTargetSound;
    uint32_t      motorMask = uploadMotorMask;
    uploadTargetSound = nullptr;
    uploadMotorMask   = 0;
    if (uploadMutexHeld) { sdMutexGive(); uploadMutexHeld = false; }
    // Ab hier halten wir den SD-Mutex NICHT mehr - LoadWavFile() darf ihn
    // jetzt gefahrlos selbst nehmen (siehe Kommentar oben bei UPLOAD_FILE_START).
    // Erfolg: laedt den Sound mit dem NEUEN Inhalt neu - sonst bliebe er bis
    // zum naechsten Trigger (Sound1-24) bzw. bis zum naechsten Reboot
    // (Motor-Loop/Shut/Start, die sonst nie neu geladen werden) stumm.
    // Fehler/Abbruch: die Datei wurde oben geloescht, LoadWavFile() schlaegt
    // dann korrekt fehl und setzt FileOK=false (kein Sound mehr auf diesem
    // Slot, bis ein neuer Upload klappt).
    if (target) target->LoadWavFile();
    // v7.11: die fuer den Upload freigegebenen Motor-Handles wieder oeffnen.
    restoreSoundHandles(motorMask);
    // v7.16: Cooldown fuer den naechsten Config-Save starten (siehe
    // markSdActivity()/SD_SAVE_COOLDOWN_MS oben) - unabhaengig davon, ob der
    // Upload erfolgreich war oder abgebrochen wurde, es wurde in jedem Fall
    // gerade auf die SD-Karte geschrieben.
    markSdActivity();
  }
}

// Wird einmal aufgerufen, NACHDEM der Upload-Callback oben fertig ist -
// schickt nur noch die JSON-Antwort anhand des in uploadFailed/uploadBytes
// gemerkten Ergebnisses.
void WebServerManager::handleApiSoundUpload() {
  String name = server.arg("name");
  if (!isValidSoundUploadName(name)) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad_name\"}");
    return;
  }
  if (uploadFailed) {
    server.send(200, "application/json", "{\"ok\":false,\"error\":\"" + uploadErrCode + "\"}");
    return;
  }
  sendJson(server, "{\"ok\":true,\"file\":\"" + name + "\",\"size\":" + String(uploadBytes) + "}");
}

// Liest, ob eine Datei existiert und wie gross sie ist - kleiner Helfer fuer
// handleApiSoundFiles(), damit dort nicht zweimal derselbe SD.open()/close()-
// Block steht.
static bool sdFileInfo(const String& path, uint32_t& outSize) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  outSize = f.size();
  f.close();
  return true;
}

// ── GET /api/soundfiles ───────────────────────────────────────────────────
// Liefert fuer alle von der Firmware erwarteten Sound-Dateien (Motor:
// loop/shut/start.wav, Sound 1-24: soundN.wav), ob sie auf der SD-Karte
// vorhanden sind und wie gross sie sind - Grundlage fuer die Datei-Verwaltung
// im Webinterface (Tab "Motor"/"Sounds": Status je Datei, Upload/Loeschen).
void WebServerManager::handleApiSoundFiles() {
  static const char* const fixedNames[3] = { "loop.wav", "shut.wav", "start.wav" };
  bool haveSd = sdMutexTake(3000);

  String j = "{\"ok\":true,\"files\":{";
  bool first = true;
  for (int i = 0; i < 3 + 24; i++) {
    String name = (i < 3) ? String(fixedNames[i]) : ("sound" + String(i - 2) + ".wav");
    uint32_t size = 0;
    bool exists = haveSd && sdFileInfo("/" + name, size);
    if (!first) j += ",";
    first = false;
    j += "\"" + name + "\":{\"exists\":" + String(exists ? "true" : "false") +
         ",\"size\":" + String(size) + "}";
  }
  j += "}}";

  if (haveSd) sdMutexGive();
  sendJson(server, j);
}

// ── POST /api/sounddelete?name=... ───────────────────────────────────────
// Loescht gezielt eine einzelne Sound-Datei von der SD-Karte (Teil der
// Datei-Verwaltung im Webinterface) - dieselbe Namenspruefung wie beim
// Upload (isValidSoundUploadName()), kein beliebiges Loeschen moeglich. Ein
// geloeschter Sound spielt beim naechsten Aufruf einfach nichts mehr ab
// (OpenWavFile() schlaegt fehl, FileOK bleibt false, siehe XT_I2S_Audio.cpp) -
// kein Absturz, keine Sonderbehandlung an anderer Stelle noetig.
void WebServerManager::handleApiSoundDelete() {
  String name = server.arg("name");
  if (!isValidSoundUploadName(name)) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad_name\"}");
    return;
  }

  // v7.09: wie beim Upload erst das evtl. gerade laufende Sound-Objekt
  // schliessen (siehe soundObjectFor()) - sonst schlaegt das Loeschen eines
  // aktiv im Loop-Modus abgespielten Sounds (offenes Datei-Handle) unnoetig
  // fehl. Muss VOR dem eigenen sdMutexTake() passieren, da UnLoadWavFile()/
  // LoadWavFile() den (nicht rekursiven) SD-Mutex selbst nehmen - sonst
  // Deadlock.
  XT_Wav_Class* target = soundObjectFor(name);
  if (target) target->UnLoadWavFile();
  // v7.11: wie beim Upload zusaetzlich die ungenutzten Motor-Handles
  // freigeben (siehe releaseIdleSoundHandles()) - ebenfalls VOR dem eigenen
  // sdMutexTake(), da UnLoadWavFile()/LoadWavFile() den Mutex selbst nehmen.
  uint32_t motorMask = releaseIdleSoundHandles(target);

  if (!sdMutexTake(3000)) {
    server.send(503, "application/json", "{\"ok\":false,\"error\":\"sd_busy\"}");
    if (target) target->LoadWavFile(); // vorherigen Zustand wiederherstellen
    restoreSoundHandles(motorMask);
    return;
  }
  bool existed = SD.exists("/" + name);
  bool ok = !existed || SD.remove("/" + name);
  sdMutexGive();

  // Mutex ist jetzt frei - LoadWavFile() darf ihn gefahrlos selbst nehmen.
  // Erfolgreich geloescht: OpenWavFile() findet die Datei nicht mehr und
  // setzt FileOK=false. Loeschen fehlgeschlagen: die alte Datei besteht
  // weiter, also neu laden statt stumm zu bleiben.
  if (target) target->LoadWavFile();
  restoreSoundHandles(motorMask);
  // v7.16: siehe Kommentar bei markSdActivity() - gilt fuer Loeschen genauso
  // wie fuer Upload.
  markSdActivity();

  if (ok) {
    sendJson(server, "{\"ok\":true,\"file\":\"" + name + "\",\"existed\":" + String(existed ? "true" : "false") + "}");
  } else {
    server.send(500, "application/json", "{\"ok\":false,\"error\":\"sd_delete\"}");
  }
}

// Fortschritt zwischen den mehrfachen Aufrufen von handleApiOtaUpdateData()
// merken - analog zu uploadFailed/uploadErrCode/uploadBytes beim Sound-
// Upload, aber ohne SD-Mutex, da hier nicht auf die SD-Karte, sondern in
// das interne OTA-Flash-Segment geschrieben wird. otaStarted haelt fest, ob
// Update.begin() erfolgreich war, damit UPLOAD_FILE_WRITE/-END nach einem
// fehlgeschlagenen Start nichts mehr tun.
static bool     otaFailed  = false;
static String   otaErrCode;
static uint32_t otaBytes   = 0;
static bool     otaStarted = false;

// ── Upload-Callback fuer POST /api/otaupdate ─────────────────────────────
// Analog zu handleApiSoundUploadData(), aber es wird nicht auf die SD-Karte,
// sondern ueber die Arduino-"Update"-Bibliothek in das gerade INAKTIVE
// OTA-Flash-Segment geschrieben (ota_0/ota_1 - siehe platformio.ini,
// board_build.partitions). Die aktuell laufende Firmware liegt im jeweils
// ANDEREN Segment und bleibt waehrend des gesamten Uploads unangetastet:
// schlaegt der Upload fehl oder wird er abgebrochen, bootet das Modul beim
// naechsten Start einfach mit der bisherigen Firmware weiter (kein
// Bricking-Risiko durch einen misslungenen Upload). Kein SD-Mutex noetig,
// da hier nicht auf die SD-Karte zugegriffen wird.
void WebServerManager::handleApiOtaUpdateData() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaFailed = false; otaErrCode = ""; otaBytes = 0; otaStarted = false;
    Serial.printf("[OTA] Start: %s\n", upload.filename.c_str());
    // Bei multipart/form-data ist die Gesamtgroesse vorab nicht bekannt -
    // UPDATE_SIZE_UNKNOWN laesst die Bibliothek bis zum Ende des jeweiligen
    // OTA-Segments schreiben; die eigentliche Vollstaendigkeitspruefung
    // passiert bei Update.end() unten.
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      otaFailed = true; otaErrCode = "update_begin";
      return;
    }
    otaStarted = true;

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (otaFailed || !otaStarted) return; // vorheriger Fehler - Rest verwerfen
    // Grobe Plausibilitaetspruefung am allerersten Haeppchen: jede gueltige
    // ESP32-Firmware beginnt mit dem Magic-Byte 0xE9. Faengt eine versehent-
    // lich falsche Datei (z.B. eine WAV- oder JSON-Datei) frueh ab, statt sie
    // komplett hochzuladen und erst bei Update.end() abzulehnen.
    if (otaBytes == 0 && upload.currentSize > 0 && upload.buf[0] != 0xE9) {
      otaFailed = true; otaErrCode = "bad_file";
      return;
    }
    size_t written = Update.write(upload.buf, upload.currentSize);
    otaBytes += written;
    if (written != upload.currentSize) {
      otaFailed = true; otaErrCode = "flash_write"; // z.B. Segment voll
    }

  } else if (upload.status == UPLOAD_FILE_END || upload.status == UPLOAD_FILE_ABORTED) {
    if (upload.status == UPLOAD_FILE_ABORTED) { otaFailed = true; otaErrCode = "aborted"; }
    if (otaStarted) {
      if (otaFailed) {
        Update.abort();
      } else if (!Update.end(true)) {
        // "true" = neue Partition als bootfaehig markieren, aber nur wenn
        // die empfangenen Bytes vollstaendig sind - schlaegt z.B. an, wenn
        // die Datei kein vollstaendiges Firmware-Image ist oder mitten in
        // der Uebertragung abgebrochen wurde, ohne dass ABORTED erkannt wird.
        otaFailed = true; otaErrCode = "update_end";
      } else {
        Serial.printf("[OTA] Fertig: %u Bytes\n", (unsigned)otaBytes);
      }
    }
  }
}

// Wird einmal aufgerufen, NACHDEM der Upload-Callback oben fertig ist -
// schickt die JSON-Antwort und startet das Modul bei Erfolg neu. Die
// Antwort wird per flush() explizit hinausgeschickt und dann kurz gewartet,
// damit sie den Browser noch erreicht, BEVOR der Neustart die WLAN-
// Verbindung (kurzzeitig) abbrechen laesst.
void WebServerManager::handleApiOtaUpdate() {
  if (otaFailed) {
    server.send(200, "application/json", "{\"ok\":false,\"error\":\"" + otaErrCode + "\"}");
    return;
  }
  sendJson(server, "{\"ok\":true,\"size\":" + String(otaBytes) + "}");
  server.client().flush();
  delay(500);
  ESP.restart();
}

// ── POST /api/wifi_enable  {"on":true/false} ─────────────────────────────
// Manueller WLAN-Schalter (Web-Tab "WiFi", entspricht Lua-Feld 174) -
// schaltet enableAP()/disableAP() (siehe weiter oben, kurz nach begin())
// sofort, OHNE die Konfiguration zu aendern oder das Modul neu zu starten.
// Bildet nur den aktuellen Laufzeit-Zustand ab - das Boot-Verhalten
// (WifiPin/GPIO13) bleibt unberuehrt.
void WebServerManager::handleApiWifiEnable() {
  bool on = false;
  if (server.hasArg("plain")) {
    const String& body = server.arg("plain");
    on = body.indexOf("\"on\":true") >= 0 || body.indexOf("\"on\": true") >= 0 || body.indexOf("\"on\":1") >= 0;
  }
  if (on) {
    enableAP();
    sendJson(server, "{\"ok\":true,\"active\":true}");
  } else {
    // Antwort ZUERST senden und explizit flushen, DANACH erst den AP
    // abschalten - sonst wuerde die eigene HTTP-Antwort evtl. nicht mehr
    // ankommen, weil die Verbindung, ueber die sie laeuft, mitten im Senden
    // wegfaellt (dieselbe Reihenfolge wie beim Neustart nach einem OTA-
    // Update, siehe handleApiOtaUpdate() oben).
    sendJson(server, "{\"ok\":true,\"active\":false}");
    server.client().flush();
    delay(200);
    disableAP();
  }
}

// ── GET /api/debug ────────────────────────────────────────────────────────
// Live-Daten: BUS-Kanäle, PWM, WAV-Status, S.Port.
void WebServerManager::handleApiDebug() {
  String j = "{";

  // BUS-Kanäle 1-16
  j += "\"channels\":[";
  for (int i = 0; i < 16; i++) { if (i) j += ","; j += String(channel_output[i]); }
  j += "],";

  // PWM Pins 1-6
  j += "\"pwm\":[";
  for (int i = 0; i < 6; i++) {
    if (i) j += ",";
    j += "{\"us\":"  + String(PWM_pulse_width[i]) + ",";
    j += "\"pct\":"  + String(map(PWM_pulse_width[i],
                                   config.PWM_scale_min,
                                   config.PWM_scale_max, 0, 100)) + "}";
  }
  j += "],";

  // WAV-Dateistatus
  j += "\"wav\":{";
  j += "\"loop\":"  + String(Sound_loop.FileOK)  + ",";
  j += "\"shut\":"  + String(Sound_shut.FileOK)  + ",";
  j += "\"start\":" + String(Sound_start.FileOK) + ",";
  // Ueber Sounds[] (siehe Extern-Deklaration oben) deckt das alle
  // RCSOUND_NUM_SLOTS-1 Sounds ab.
  j += "\"s\":[";
  for (int i = 1; i < RCSOUND_NUM_SLOTS; i++) {
    if (i > 1) j += ",";
    j += String(Sounds[i] ? Sounds[i]->FileOK : 0);
  }
  j += "]},";

  // S.Port Live-Daten
  j += "\"hw_config\":"  + String(config.Hardware_Config) + ",";
  // Der AKTIVE (Boot-)Wert, nicht der evtl. schon geaenderte, aber noch
  // nicht neu gestartete config.PortB_Mode.
  j += "\"portb_mode\":" + String(PortB_Mode_boot) + ",";
  j += "\"sport\":{";
  j += "\"total_v\":"  + String(sportGetTotalVoltage(), 2)      + ",";
  j += "\"min_cell\":" + String(sportGetMinCell(), 3)           + ",";
  j += "\"cells\":"    + String(sportGetTotalCells())           + ",";
  j += "\"soc\":"      + String(sportCalcSoC(sportGetMinCell()))+ ",";
  // Diagnose-Zaehler (Learning aus ESP32-GPS-Telemetry): machen sichtbar,
  // OB ueberhaupt Bytes/Polls/CRC-gueltige Frames ankommen, statt nur
  // online/offline zu zeigen.
  j += "\"raw_bytes\":"    + String(sportGetRawBytesRx())      + ",";
  j += "\"polls_sent\":"   + String(sportGetPollsSent())       + ",";
  j += "\"valid_frames\":" + String(sportGetValidFrames())     + ",";
  j += "\"crc_errors\":"   + String(sportGetCrcErrors())       + ",";
  j += "\"no_response\":"  + String(sportGetNoResponseCount()) + ",";
  char lastPollHex[5];
  snprintf(lastPollHex, sizeof(lastPollHex), "0x%02X", sportGetLastPollId());
  j += "\"last_poll_id\":\"" + String(lastPollHex) + "\",";
  j += "\"packs\":[";
  for (uint8_t i = 0; i < 2; i++) {
    if (i) j += ",";
    j += "{\"online\":"  + String(lipoSensor[i].online ? "true" : "false") + ",";
    j += "\"cells\":"    + String(lipoSensor[i].cellCount)      + ",";
    j += "\"total\":"    + String(lipoSensor[i].totalVoltage, 2)+ ",";
    j += "\"min\":"      + String(lipoSensor[i].minCell, 3)     + ",";
    j += "\"cv\":[";
    for (uint8_t c = 0; c < 6; c++) {
      if (c) j += ",";
      j += String(lipoSensor[i].cellVoltage[c], 3);
    }
    j += "]}";
  }
  j += "]}";

  // GPS-Status (Port C)
  j += ",\"gps\":{";
  j += "\"active\":" + String(PortC_GPS_Enabled_boot ? 1 : 0) + ",";
  j += "\"speed\":"  + String(gpsGetSpeedKmh(), 1) + ",";
  j += "\"sats\":"   + String(gpsGetSatellites())  + ",";
  j += "\"fix\":"    + String(gpsHasFix() ? 1 : 0)  + ",";
  j += "\"pinConflict\":" + String(gpsPinConflict ? 1 : 0);
  j += "}";

  // CRSF-Diagnosezaehler (nur relevant bei RC-System==4, aber immer gefuellt)
  j += ",\"crsf_diag\":{";
  j += "\"raw_bytes\":"     + String(crsf.getRawBytesRx())  + ",";
  j += "\"valid_frames\":"  + String(crsf.getValidFrames()) + ",";
  j += "\"crc_errors\":"    + String(crsf.getCrcErrors())   + ",";
  j += "\"device_pings\":"  + String(crsf.getDevicePings()) + ",";
  j += "\"param_reads\":"   + String(crsf.getParamReads())  + ",";
  j += "\"param_writes\":"  + String(crsf.getParamWrites());
  j += "}";

  // WLAN-Live-Status - AP gerade aktiv? RC-Signal gerade da? (fuer die
  // Karte "WLAN jetzt" im Web-Tab "WiFi", siehe renderWifiLiveStatus()).
  j += ",\"wifi\":{";
  j += "\"active\":" + String(WebServerManager::isApActive() ? "true" : "false") + ",";
  j += "\"bus_ok\":" + String(BUS_OK ? "true" : "false");
  j += "}";

  j += "}";
  sendJson(server, j);
}

// ── POST /api/sound ───────────────────────────────────────────────────────
// Sound-Test:      {"id":1,"test":true}
// Config schreiben: {"id":1,"source":70,"vol":100,"mode":0}
// Motor (id=0):    {"id":0,"speed_src":1,"motor_mode":0,"rpm_min":100,...}
void WebServerManager::handleApiSound() {
  if (!server.hasArg("plain")) { sendErr(server); return; }
  const String& body = server.arg("plain");

  int id = -1;
  // Grenze ist RCSOUND_NUM_SLOTS-1 (25 Sounds insgesamt, Index 0 = Motor).
  if (!jsonGetInt(body, "id", id) || id < 0 || id > RCSOUND_NUM_SLOTS - 1) { sendErr(server); return; }

  bool changed = false;
  int  v;

  if (body.indexOf("\"test\":true") >= 0) Sound_on_web[id] = true;

  // Ein Rohwert, den einTyp() (ESP32-RC-Sound.ino) nicht als gueltig erkennt
  // (z.B. aus einer alten Konfigurationsdatei mit einem inzwischen
  // entfallenen Wertebereich), wird auf 999 (Deaktiviert) zurueckgesetzt -
  // siehe isValidEinSource(). Greift auch bei jedem Konfigurations-Import.
  if (jsonGetInt(body, "source", v)) {
    if (!isValidEinSource(v)) {
      Serial.printf("[API] Sound %d: ungueltiger Quellwert %d abgelehnt -> auf Deaktiviert (999) gesetzt.\n", id, v);
      v = 999;
    }
    config.Source_Start_Sound[id] = v;                    changed = true;
  }
  if (jsonGetInt(body, "vol",    v)) { config.Volumen_Sound[id]       = constrain(v,0,200);  changed = true; }
  if (jsonGetInt(body, "mode",   v)) { config.Mode_Sound[id]          = constrain(v,0,2);    changed = true; }

  if (id == 0) {  // Motor-Parameter
    if (jsonGetInt(body, "speed_src",  v)) { config.Source_Speed_Sound_0  = v;                     changed = true; }
    if (jsonGetInt(body, "motor_mode", v)) { config.throttle_mode          = constrain(v,0,1);     changed = true; }
    if (jsonGetInt(body, "toggle",     v)) { config.engine_on_toggle       = constrain(v,0,1);     changed = true; }
    if (jsonGetInt(body, "rpm_min",    v)) { config.Min_Speed_Sound_0      = constrain(v,0,200);   changed = true; }
    if (jsonGetInt(body, "rpm_max",    v)) { config.Max_Speed_Sound_0      = constrain(v,100,600); changed = true; }
    if (jsonGetInt(body, "shutdown_s", v)) { config.shutdowndelay          = constrain(v,0,60);    changed = true; }
    if (jsonGetInt(body, "ramp",       v)) { config.throttle_ramp          = constrain(v,0,50);    changed = true; }
    if (jsonGetInt(body, "deadband",   v)) { config.throttle_dead_band     = constrain(v,0,50);    changed = true; }
  }

  if (changed) markDirty();
  sendOk(server);
}

// ── Haupt-Request-Handler ────────────────────────────────────────────────
// Alle GET-Requests landen hier (onNotFound + on("/"))
void WebServerManager::handleRequest() {
  const String uri = server.uri();

  // ── Navigation ────────────────────────────────────────────────────────
  if (uri == "/next" || server.hasArg("next"))      { Menu++; }
  if (uri == "/back" || server.hasArg("back"))      { Menu--; }
  if (server.hasArg("menu"))  { Menu = server.arg("menu").toInt(); }
  // Menu kommt roh aus ?menu=... - safeMenu begrenzt ihn auf einen gueltigen
  // Index in Sound_on_web[] (Groesse RCSOUND_NUM_SLOTS), BEVOR er hier
  // benutzt wird, damit ein beliebiger Client-Wert nicht ausserhalb des
  // Arrays schreiben kann.
  uint8_t safeMenu = (uint8_t)constrain(Menu, 0, RCSOUND_NUM_SLOTS - 1);
  if (uri == "/Sound/on")  { Sound_on_web[safeMenu] = true; }
  // v7.16: kein sofortiges saveConfigForce() mehr hier (siehe Kommentar bei
  // handleApiConfigPost()) - nur noch markDirty(), der eigentliche Schreib-
  // vorgang laeuft ueber die debounced+cooldown-geschuetzte Pruefung in
  // loop().
  if (uri == "/save")      { markDirty(); }
  if (uri == "/reset")     { Reset_all(); }
  if (uri == "/setsbus")   { set_sbus(); }
  if (uri == "/setpwm")    { set_pwm(); }
  if (uri == "/setpin")    { set_pin(); }

  // ── PWM Skalierung (eigene Route /setPWM?pwmMin=X&pwmMax=Y) ──────────
  if (uri == "/setPWM") {
    if (server.hasArg("pwmMin")) { config.PWM_scale_min = constrain(server.arg("pwmMin").toInt(),0,4095); markDirty(); }
    if (server.hasArg("pwmMax")) { config.PWM_scale_max = constrain(server.arg("pwmMax").toInt(),0,4095); markDirty(); }
  }

  // safeMenu ist bereits oben bei "/Sound/on" berechnet (siehe dort) und
  // wird fuer die Array-Zugriffe unten (Volumen_Sound[safeMenu] etc.)
  // verwendet - NICHT das unbegrenzte Menu selbst, das ausserhalb der
  // Arrays liegen koennte (Speicherkorruption bei z.B. "?menu=99&...").

  // ── Numerische Parameter (alle via /?KEY=VAL& ) ───────────────────────
  // Dieselben Grenzen wie in handleApiSound()/handleApiConfigPost() - anders
  // als die moderne /api/*-Route liefen diese Legacy-Parameter sonst
  // ungeprueft per toInt() (ungueltige Quellen, negative Lautstaerken oder
  // widerspruechliche PWM-Grenzen waeren moeglich).
  // WebServer.h: server.arg("KEY") liefert den Wert, server.hasArg() prüft Existenz
  if (server.hasArg("VolumenSound"))         { config.Volumen_Sound[safeMenu]      = constrain(server.arg("VolumenSound").toInt(),0,200);      markDirty(); }
  if (server.hasArg("Drehzahlmin"))          { config.Min_Speed_Sound_0            = constrain(server.arg("Drehzahlmin").toInt(),0,200);       markDirty(); }
  if (server.hasArg("Drehzahlmax"))          { config.Max_Speed_Sound_0            = constrain(server.arg("Drehzahlmax").toInt(),100,600);     markDirty(); }
  if (server.hasArg("shutdowndelay"))        { config.shutdowndelay                = constrain(server.arg("shutdowndelay").toInt(),0,60);      markDirty(); }
  if (server.hasArg("MotorMODE"))            { config.throttle_mode                = constrain(server.arg("MotorMODE").toInt(),0,1);           markDirty(); }
  if (server.hasArg("MotorOnToggle"))        { config.engine_on_toggle             = constrain(server.arg("MotorOnToggle").toInt(),0,1);       markDirty(); }
  if (server.hasArg("SoundON"))              { config.Source_Start_Sound[safeMenu] = server.arg("SoundON").toInt();                            markDirty(); }
  if (server.hasArg("SoundSPEED"))           { config.Source_Speed_Sound_0         = server.arg("SoundSPEED").toInt();                         markDirty(); }
  if (server.hasArg("RCSytem"))              { config.Einkanal_RC_System           = constrain(server.arg("RCSytem").toInt(),0,4);             markDirty(); }
  if (server.hasArg("SbuschannelEinkanal"))  { config.Einkanal_Channel             = constrain(server.arg("SbuschannelEinkanal").toInt(),0,999); markDirty(); }
  if (server.hasArg("MODULADRESSE"))         { config.modul_adress                 = constrain(server.arg("MODULADRESSE").toInt(),0,20);       markDirty(); }
  if (server.hasArg("SportPollID0"))         { config.sport_poll_id[0]             = (uint8_t)strtol(server.arg("SportPollID0").c_str(), nullptr, 16); markDirty(); }
  if (server.hasArg("SportPollID1"))         { config.sport_poll_id[1]             = (uint8_t)strtol(server.arg("SportPollID1").c_str(), nullptr, 16); markDirty(); }
  if (server.hasArg("EinkanalMode"))         { config.Einkanal_mode                = server.arg("EinkanalMode").toInt();                       markDirty(); }
  if (server.hasArg("SoundMODE"))            { config.Mode_Sound[safeMenu]         = constrain(server.arg("SoundMODE").toInt(),0,2);           markDirty(); }
  if (server.hasArg("ThrottleRamp"))         { config.throttle_ramp                = constrain(server.arg("ThrottleRamp").toInt(),0,50);       markDirty(); }
  if (server.hasArg("ThrottleDeadBand"))     { config.throttle_dead_band           = constrain(server.arg("ThrottleDeadBand").toInt(),0,50);   markDirty(); }
  if (server.hasArg("HardwareConfig"))       { config.Hardware_Config_Pending      = constrain(server.arg("HardwareConfig").toInt(),0,2);      markDirty();
    Serial.printf("[Legacy] Hardware_Config_Pending -> %d (wirkt erst nach Neustart)\n", config.Hardware_Config_Pending); }

  // ── String-Parameter (URL-dekodiert) ─────────────────────────────────
  if (server.hasArg("WiFi_SSID")) {
    valueString = urlDecode(server.arg("WiFi_SSID"));
    strncpy(config.WiFi_SSID, valueString.c_str(), sizeof(config.WiFi_SSID)-1);
    config.WiFi_SSID[sizeof(config.WiFi_SSID)-1] = '\0'; markDirty();
  }
  if (server.hasArg("WiFi_Password")) {
    valueString = urlDecode(server.arg("WiFi_Password"));
    strncpy(config.WiFi_Password, valueString.c_str(), sizeof(config.WiFi_Password)-1);
    config.WiFi_Password[sizeof(config.WiFi_Password)-1] = '\0'; markDirty();
  }
  if (server.hasArg("WiFi_IP")) {
    valueString = urlDecode(server.arg("WiFi_IP"));
    strncpy(config.WiFi_IP, valueString.c_str(), sizeof(config.WiFi_IP)-1);
    config.WiFi_IP[sizeof(config.WiFi_IP)-1] = '\0'; markDirty();
  }
  if (server.hasArg("Device_Name")) {
    valueString = urlDecode(server.arg("Device_Name"));
    strncpy(config.Device_Name, valueString.c_str(), sizeof(config.Device_Name)-1);
    config.Device_Name[sizeof(config.Device_Name)-1] = '\0'; markDirty();
  }

  Menu = constrain(Menu, 0, 12);

  // HTML direkt aus Flash senden – 0 Bytes RAM, sofortiger TTFB
  server.send_P(200, "text/html", RCSOUND_HTML);
}

// ── HTML-Seitenaufbau ────────────────────────────────────────────────────
