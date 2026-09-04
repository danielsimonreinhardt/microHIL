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
| `*IDN?` | `*IDN?` | `microHIL,fw=0.1.0` |
| `RELAY <1-4> <0\|1>` | `RELAY 1 1` | `OK` |
| `RELAY? <1-4>` | `RELAY? 1` | `1` |
| `OUT <1-8> <0\|1>` | `OUT 3 0` | `OK` |
| `OUT? <1-8>` | `OUT? 3` | `0` |
| `IN? <1-8>` | `IN? 5` | `1` |
| `IN?` (ohne Nr.) | `IN?` | `10110001` (IN1 zuerst) |
| `AOUT <1-2> <mV>` | `AOUT 1 1650` | `OK` |
| `AIN? <1-4>` | `AIN? 2` | `3300` |
| `PWR12 <1-2> <0\|1>` | `PWR12 1 1` | `OK` |
| `PWR12? <1-2>` | `PWR12? 1` | `1` |
| `CURR? <1-2>` | `CURR? 1` | `536` |
| `PWM <1-4> <0-1000>` | `PWM 1 500` | `OK` |
| `PWM? <1-4>` | `PWM? 1` | `500` |

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

- `AOUT <1-2> <mV>`: mV wird auf `0..3300` geklemmt (DAC-Referenz).
  `AOUT 1 99999` antwortet `OK`, setzt aber effektiv 3300 mV — der Aufrufer
  erfährt aus der Antwort nicht, dass der Wert verändert wurde.
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

## Timing

Alle Kommandos außer `AIN?` und `CURR?` antworten praktisch sofort
(reine GPIO-/Register-Operationen). `AIN?` und `CURR?` lösen intern eine
Single-Conversion-ADC-Messung aus (`HAL_ADC_PollForConversion`, 10 ms
Timeout) — im Normalfall deutlich schneller, aber ein Treiber, der viele
Analogkanäle im Poll-Takt abfragt, sollte pro Aufruf mit bis zu ~10 ms
rechnen statt mit der sonst üblichen Sub-Millisekunden-Antwortzeit.

## Bekannte Lücke

`CURR?` liefert aktuell die rohe Sense-Spannung in mV, **keine** Umrechnung
in mA — dafür fehlt der Shunt-/Verstärkungsfaktor der Stromsense-Schaltung
auf `AIN_12VOUT1/2_CURRSENSE` (PA6/PA7). Sobald der Wert feststeht, in
`Core/Src/protocol.c` (`cmd_curr_query`) ergänzen.

## Implementierung

- Firmware: `Core/Src/protocol.c` — Ringpuffer wird in `CDC_Receive_FS`
  (USB_DEVICE/App/usbd_cdc_if.c) gefüllt, `Protocol_Poll()` läuft in der
  Main-Loop und wertet vollständige Zeilen aus.
- Host: `host/microhil.py` (Klasse `MicroHIL`, pyserial-basiert)
