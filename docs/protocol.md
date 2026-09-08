# microHIL-Kommandoprotokoll (USB-CDC)

ASCII-Zeilenprotokoll. **Anfragen** sind `\n`-terminiert (`\r\n` wird
ebenfalls akzeptiert), 115200 Baud (wird vom virtuellen COM-Port ignoriert,
aber pyserial verlangt einen Wert). **Antworten** enden immer mit `\r\n` —
auch `OK` und `ERR ...` — ein simples `readline()` reicht also zum Auslesen.
Werte sind immer Ganzzahlen in mV/mA, nie Fließkomma.

Kommandos sind **case-sensitiv, nur Großschreibung** (`RELAY`, nicht
`relay`), Verb und Argumente durch Leerzeichen oder Tab getrennt
(`strtok(line, " \t")` — mehrere Trenner hintereinander sind unproblematisch).
Eine **leere Zeile erzeugt keine Antwort** (weder `OK` noch `ERR`) — ein
Treiber, der eine Leerzeile schickt, würde sonst in den Timeout laufen statt
einen Fehler zu sehen.

**USB-Identifikation:** VID:PID `0483:5740` (ST-Werks-VCP-IDs, kein eigenes
USB-Zertifikat). microHIL meldet zwei virtuelle COM-Ports mit dieser
VID:PID. Dieses Protokoll liegt auf dem **ersten** (Interface 0, Windows
`MI_00`, Linux `-if00`); auf dem zweiten liegt CAN1 als SLCAN-Interface,
siehe [can-usb.md](can-usb.md). Den richtigen Port findet `host/microhil.py`
selbst (`find_port()`) — die Interface-Nummer ist bei zwei gleichzeitig
sichtbaren Ports mit identischer VID:PID das einzig verlässliche
Unterscheidungsmerkmal, siehe dort für die Details je Plattform.

| Befehl | Beispiel | Antwort |
|---|---|---|
| `*IDN?` | `*IDN?` | `microHIL,fw=0.1.0,SN=2065386A5631` |
| `RELAY <1-4> <0\|1>` | `RELAY 1 1` | `OK` |
| `RELAY? <1-4>` | `RELAY? 1` | `1` |
| `OUT <1-8> <0\|1>` | `OUT 3 0` | `OK` |
| `OUT? <1-8>` | `OUT? 3` | `0` |
| `IN? <1-8>` | `IN? 5` | `1` |
| `IN?` (ohne Nr.) | `IN?` | `10110001` (IN1 zuerst) |
| `AOUT <1-2> <mV>` | `AOUT 1 1650` | `OK` |
| `AOUTRAW <1-2> <mV>` | `AOUTRAW 1 1650` | `OK` |
| `AIN? <1-4>` | `AIN? 2` | `3300` |
| `AINRAW? <1-4>` | `AINRAW? 2` | `818` |
| `PWR12 <1-2> <0\|1>` | `PWR12 1 1` | `OK` |
| `PWR12? <1-2>` | `PWR12? 1` | `1` |
| `PWR12FLT? <1-2>` | `PWR12FLT? 1` | `1` |
| `ILIM <1-2> <mA>` | `ILIM 1 1000` | `OK` |
| `ILIM? <1-2>` | `ILIM? 1` | `1000` |
| `CURR? <1-2>` | `CURR? 1` | `536` |
| `CURRRAW? <1-2>` | `CURRRAW? 1` | `536` |
| `PWM <1-4> <0-1000>` | `PWM 1 500` | `OK` |
| `PWM? <1-4>` | `PWM? 1` | `500` |

`*IDN?`s `SN=`-Feld ist aus der 96-bit STM32-UID abgeleitet und identisch mit
der USB-Seriennummer (unter Windows als `SER=...` in der `hwid` sichtbar,
siehe `serial.tools.list_ports`) — eindeutig je physischem Board, geeignet um
ein bestimmtes Exemplar wiederzuerkennen (z. B. um bekannte Hardware-Defekte
je Board auszublenden, siehe `docs/hardware-notes.md`). `host/microhil.py`
stellt das über `MicroHIL.get_serial()` bereit.

`AIN?`/`CURR?` liefern kalibrierte physikalische Werte (mV bzw. mA), `AOUT`
nimmt die gewuenschte physikalische Ausgangsspannung entgegen - siehe
[calibration.md](calibration.md) für die Kalibrierkonstanten und die
Messprozedur. `AINRAW?`/`CURRRAW?`/`AOUTRAW` sprechen stattdessen direkt den
unkalibrierten ADC-/DAC-Wert an (raw*3300/4095 bzw. DAC-Sollwert 0..3300 mV,
ohne Anwendung der Kalibrierkonstanten) - fuer die Kalibrierprozedur selbst
und zu Diagnosezwecken, im Normalbetrieb nicht benoetigt.

`PWM`-Duty-Cycle in Promille (0 = aus, 1000 = 100%).

Bei ungültigen/unbekannten Kommandos: `ERR <Grund>`. Es gibt genau drei
Gründe, keine weiteren:

| Grund | Bedeutung |
|---|---|
| `ERR ARGS` | Argument fehlt (z. B. `RELAY 1` ohne Zustand) |
| `ERR RANGE` | **Kanal-/Indexargument** außerhalb des gültigen Bereichs (z. B. `RELAY 5 1`) |
| `ERR UNKNOWN` | Kommando-Verb nicht erkannt |

## Wertebereiche: Index vs. Nutzwert

Wichtige Asymmetrie, die sich nicht aus der Tabelle oben ableiten lässt:
**nur das Kanal-/Indexargument wird geprüft und liefert `ERR RANGE`.** Das
eigentliche Nutzwertargument (mV bei `AOUT`, Promille bei `PWM`) wird **ohne
Fehlermeldung auf den gültigen Bereich geklemmt**, nicht abgelehnt:

- `AOUT <1-2> <mV>`: mV ist die gewünschte physikalische Ausgangsspannung und
  wird auf den kalibrierten Ausgangsbereich geklemmt (nominal ca. `0..12210`
  mV, siehe `cal_aout` in `Core/Src/calibration.c` bzw.
  [calibration.md](calibration.md) — die genauen Grenzen ändern sich mit der
  Kalibrierung). `AOUT 1 99999` antwortet `OK`, setzt aber effektiv den
  Maximalwert — der Aufrufer erfährt aus der Antwort nicht, dass der Wert
  verändert wurde. `AOUTRAW <1-2> <mV>` (unkalibrierter DAC-Sollwert) klemmt
  stattdessen weiterhin auf `0..3300`.
- `PWM <1-4> <permille>`: Promille wird auf `0..1000` geklemmt. `PWM 1 -5`
  antwortet `OK` und setzt effektiv 0.

Für einen Treiber heißt das: `OK` bei `AOUT`/`PWM` bestätigt nur, dass der
Kanalindex gültig war — **nicht**, dass der genaue angeforderte Wert übernommen
wurde. Wer das prüfen will, muss den Wert per `AOUT?`/`PWM?`
zurücklesen (Anmerkung: `AOUT?` existiert nicht, nur `PWM?`).

`RELAY`/`OUT`/`PWR12` erwarten `0`/`1`, akzeptieren aber jeden Ganzzahlwert
(`atoi`) — jeder Wert ungleich `0` wird als `1` (ein) gewertet, es gibt keine
eigene Prüfung auf exakt `0`/`1`.

## Verriegelung PWM1-4 / OUT1-4

PC6-PC9 (PWM1-4, TIM3) und OUT1-4 (PA10/PA15/PC10/PC11) treiben laut
Schaltplan dieselbe Endstufe und dürfen nie gleichzeitig aktiv sein.
Firmwareseitig erzwungen: `OUT <n> 1` (n=1-4) stoppt PWM-Kanal n zwangsweise
(Duty auf 0), `PWM <n> <permille>` mit `permille > 0` schaltet OUT n
zwangsweise ab. Es gibt dafür keine eigene Fehlermeldung — die Verriegelung
wirkt still, der jeweils andere Kanal wird einfach deaktiviert.

## Strombegrenzung PWR12-1/2

Firmwareseitige Schutzfunktion (`Core/Src/protocol.c`, `pwr12_guard_poll()`),
unabhängig von den Polyfuses F1/F13/F14 (siehe `docs/hardware-notes.md`,
"Maximal zulässiger Dauerstrom") — diese sind weiterhin die letzte
Hardware-Absicherung, die Firmware soll aber im Normalbetrieb schon vorher
abschalten.

**Pro Kanal:** `ILIM <1-2> <mA>` setzt das Stromlimit für PWR12-1/2, Default
nach Reset ist **1200 mA**. Wird das Limit **ununterbrochen länger als
100 ms** überschritten (CURR1/2 werden dafür intern alle 2 ms gesampelt),
schaltet die Firmware den betroffenen Kanal ab, unabhängig von der zuletzt
per `PWR12` gesetzten Schaltanforderung. Kurze Einschaltstromspitzen unter
100 ms lösen die Verriegelung nicht aus.

**Gesamtbudget:** Der 12V-Eingang versorgt neben PWR12-1/2 auch die MCU,
deshalb gibt es zusätzlich eine von den Einzellimits unabhängige Prüfung:
überschreitet CURR1+CURR2 in Summe **1500 mA** (Ihold der gemeinsamen
Polyfuse F1, siehe `docs/hardware-notes.md`), schaltet die Firmware
**sofort** (ohne die 100-ms-Entprellung) **beide** Kanäle ab.

**Verriegelung:** In beiden Fällen bleibt der Ausgang aus, auch wenn die
Schaltanforderung (`PWR12 <n> 1`) weiter ansteht — ein einfaches erneutes
`PWR12 <n> 1` schaltet NICHT wieder ein. Der betroffene Kanal muss zuerst
per `PWR12 <n> 0` zurückgenommen werden (das löscht seine eigene
Überstromverriegelung); die Gesamtbudget-Verriegelung erlischt erst, wenn
**beide** Kanäle zurückgenommen wurden. `PWR12? <n>` liest weiterhin nur den
tatsächlichen GPIO-Zustand (0, solange eine Verriegelung aktiv ist);
`PWR12FLT? <n>` liefert eine Bitmaske, warum ein Kanal trotz anstehender
Schaltanforderung aus ist: Bit0 = eigenes Überstromlimit ausgelöst, Bit1 =
Gesamtbudget ausgelöst (`0` = kein Fault).

`ILIM` klemmt negative Werte auf 0, prüft aber sonst nicht gegen die
Hardwaregrenzen (≤1,5A Dauerstrom pro Kanal laut Sicherung) — wer ein
höheres Limit setzt, verlässt sich allein auf die Polyfuses.

## Timing

Alle Kommandos außer `AIN?` und `CURR?` antworten praktisch sofort
(reine GPIO-/Register-Operationen). `AIN?` und `CURR?` lösen intern eine
Single-Conversion-ADC-Messung aus (`HAL_ADC_PollForConversion`, 10 ms
Timeout) — im Normalfall deutlich schneller, aber ein Treiber, der viele
Analogkanäle im Poll-Takt abfragt, sollte pro Aufruf mit bis zu ~10 ms
rechnen statt mit der sonst üblichen Sub-Millisekunden-Antwortzeit.

## Kalibrierung

`AIN?`, `AOUT` und `CURR?` rechnen über feste 2-Punkt-Kalibrierkonstanten
(`Core/Src/calibration.c`) zwischen dem naiven ADC-/DAC-Wert und dem
physikalischen Wert um. Bis zur echten Messung stehen dort nominelle,
aus dem Schaltplan abgeleitete Platzhalterwerte — siehe
[calibration.md](calibration.md) für Herleitung und Messprozedur.

## Implementierung

- Firmware: `Core/Src/protocol.c` — Ringpuffer wird in `CDC_Receive_FS`
  (USB_DEVICE/App/usbd_cdc_if.c) gefüllt, `Protocol_Poll()` läuft in der
  Main-Loop und wertet vollständige Zeilen aus.
- Host: `host/microhil.py` (Klasse `MicroHIL`, pyserial-basiert)
