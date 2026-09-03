# microHIL-Kommandoprotokoll (USB-CDC)

ASCII-Zeilenprotokoll, `\n`-terminiert (`\r\n` wird ebenfalls akzeptiert),
115200 Baud (wird vom virtuellen COM-Port ignoriert, aber pyserial verlangt
einen Wert). Werte sind immer Ganzzahlen in mV/mA, nie Fließkomma.

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

Bei ungültigen/unbekannten Kommandos: `ERR <Grund>` (z. B. `ERR RANGE`,
`ERR ARGS`, `ERR UNKNOWN`).

## Verriegelung PWM1-4 / OUT1-4

PC6-PC9 (PWM1-4, TIM3) und OUT1-4 (PA10/PA15/PC10/PC11) treiben laut
Schaltplan dieselbe Endstufe und dürfen nie gleichzeitig aktiv sein.
Firmwareseitig erzwungen: `OUT <n> 1` (n=1-4) stoppt PWM-Kanal n zwangsweise
(Duty auf 0), `PWM <n> <>0>` schaltet OUT n zwangsweise ab. Es gibt dafür
keine eigene Fehlermeldung — die Verriegelung wirkt still, der jeweils
andere Kanal wird einfach deaktiviert.

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
