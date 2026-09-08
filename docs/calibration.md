# Kalibrierung: AIN, AOUT, CURR

`AIN?`, `AOUT` und `CURR?` liefern/erwarten physikalische Werte (mV bzw. mA),
keine rohen ADC-/DAC-Werte. Die Umrechnung passiert über eine lineare
2-Punkt-Kalibrierung je Kanal in `Core/Src/calibration.c`
(`cal_ain[4]`, `cal_curr[2]`, `cal_aout[2]`). Jeder Eintrag ist ein Paar
gemessener Wertepaare `{x1, y1, x2, y2}`, dazwischen (und leicht darüber
hinaus) wird linear interpoliert/extrapoliert (`Cal_Apply()` in
`calibration.c`).

**Aktueller Stand (2026-09-08): alle sechs Kanäle real durchkalibriert und
gegen Multimeter/Amperemeter verifiziert.**
- AOUT1-2 (Messpunkte 500/2800 mV Rohwert, siehe `cal_aout`): `AOUT 1/2 5000`
  → 4,989 V / 4,990 V (< 0,3 % Abweichung).
- AIN1-4 (Messpunkte ca. 1,308V/10,99V, siehe `cal_ain`): `AIN? 4` liefert bei
  10,99V angelegter Spannung exakt 10990 mV.
- CURR1-2 (Messpunkte 0,3A/1,2A, siehe `cal_curr`): `CURR? 1/2` liefert bei
  700mA echtem Laststrom (Amperemeter) 699 mA auf beiden Kanälen
  (< 0,2 % Abweichung). Vorausgegangene Hardware-Defekte (Q22+Q28 an
  PWR12-1, U18/U19 nicht bestückt) wurden am 2026-09-08 behoben, siehe
  `docs/hardware-notes.md`.

**Wichtig, dauerhaft zu beachten:** Der maximal zulässige Dauerstrom für
PWR12/CURR ist NICHT der schaltplan-abgeleitete Vollausschlag (2,5A),
sondern durch die Polyfuses F1/F13/F14 auf **1,5A** begrenzt (F1 gemeinsam
für beide Kanäle — bei gleichzeitiger Nutzung beider PWR12-Ausgänge muss
deren Summe unter 1,5A bleiben). Details:
`docs/hardware-notes.md`, Abschnitt "Maximal zulässiger Dauerstrom".

## Schaltungs-Herleitung (micro_HIL_v0.1)

Quelle: `ELECTRICS/micro_HIL_v0.1` (KiCad), Sheets `Input-Driver.kicad_sch`
(AIN) und `Output-Driver.kicad_sch` (AOUT, CURR).

- **AIN1-4** (PA3/PA0/PA1/PA2): Spannungsteiler 100 kΩ (R127/129/131/133,
  nach Signal) / 33 kΩ (R128/130/132/134, nach GND) vor einem
  Unity-Gain-Puffer (TLV6001). Teilerfaktor `(100k+33k)/33k = 4.0303`, d. h.
  0..3300 mV am ADC-Pin (PA3/PA0/PA1/PA2) entsprechen nominell 0..13300 mV
  am AIN-Eingang.
- **CURR1-2** (PA6/PA7, Sense für 12V_OUT1/2): Shunt R73/R74 = 50 mΩ
  (Ohmite LVK24, `LVK24R050CER`) + INA240A1D (Gain 20 V/V, REF1/REF2 auf
  GND). `V_sense[mV] = I[mA] · 0.05 Ω · 20 = I[mA]` — die Schaltung ist so
  dimensioniert, dass 1 mV Sense-Spannung nominell genau 1 mA Laststrom
  entspricht.
- **AOUT1-2** (PA5/PA4 = DAC_OUT2/1): Nichtinvertierender OPA990-Verstärker,
  Gain `1 + R63/R62 = 1 + 27k/10k = 3.7` (Kanal 2 analog mit R79/R78,
  gleiche Werte). Nominell 0..3300 mV DAC-Sollwert → 0..12210 mV am Ausgang.
  **Wichtig:** Dahinter liegt ein BC817/BC807-Gegentaktpuffer
  (Basisstromverstärkung für Lastfähigkeit), der **nicht** in der
  Gegenkopplungsschleife des OpAmp liegt (Feedback R63/R79 zweigt vor den
  Transistoren ab) — ein Offset durch nicht kompensierte Vbe-Abfälle ist
  möglich und zeigt sich erst in der realen Messung, nicht in der
  Schaltplan-Herleitung.

## Diagnose-Kommandos für die Kalibrierung

Zusätzlich zu `AIN?`/`AOUT`/`CURR?` (die die Kalibrierung anwenden) gibt es
unkalibrierte Gegenstücke, die direkt den rohen ADC-/DAC-Wert ansprechen:

| Kommando | Bedeutung |
|---|---|
| `AINRAW? <1-4>` | Roh-ADC-Wert in mV (`raw*3300/4095`), ohne `cal_ain` |
| `CURRRAW? <1-2>` | Rohe Sense-Spannung in mV, ohne `cal_curr` |
| `AOUTRAW <1-2> <mV>` | DAC-Sollwert direkt setzen (0..3300), ohne `cal_aout` |

Host-seitig: `MicroHIL.get_ain_raw_mv()`, `get_curr_raw_mv()`,
`set_aout_raw_mv()` in `host/microhil.py`.

## Messprozedur

Für jeden Kanal zwei Referenzpunkte messen, die den Nutzbereich möglichst
weit aufspannen (Faustregel: ca. 10 % und 80–90 % des Vollausschlags — zu
eng beieinander liegende Punkte verstärken den Effekt von Messrauschen auf
die berechnete Steigung).

### AIN1-4

1. Bekannte, präzise Spannung `V1` an AIN `n` anlegen (kalibrierte
   Spannungsquelle, mit Multimeter am Eingang nachgemessen).
2. `AINRAW? n` abfragen → `x1`. Multimeter-Wert (in mV) → `y1`.
3. Zweite Spannung `V2` anlegen, `AINRAW? n` → `x2`, Multimeter → `y2`.
4. In `Core/Src/calibration.c`: `cal_ain[n-1] = {x1, y1, x2, y2};`
5. Neu bauen/flashen, mit `AIN? n` gegen das Multimeter verifizieren.

### CURR1-2

1. Bekannten, präzisen Laststrom `I1` durch 12V-OUT `n` treiben
   (elektronische Last oder definierter Widerstand an bekannter Spannung),
   mit einem Amperemeter in Reihe nachgemessen.
2. `CURRRAW? n` abfragen → `x1`. Amperemeter-Wert (in mA) → `y1`.
3. Zweiten Strom `I2` einstellen, `CURRRAW? n` → `x2`, Amperemeter → `y2`.
4. In `calibration.c`: `cal_curr[n-1] = {x1, y1, x2, y2};`
5. Neu bauen/flashen, mit `CURR? n` gegen das Amperemeter verifizieren.

### AOUT1-2

1. `AOUTRAW n <x1>` senden (z. B. `500`), tatsächliche Ausgangsspannung mit
   Multimeter messen → `y1`.
2. `AOUTRAW n <x2>` senden (z. B. `2800`), messen → `y2`.
3. In `calibration.c`: `cal_aout[n-1] = {x1, y1, x2, y2};`
4. Neu bauen/flashen, mit `AOUT n <ziel_mV>` + Multimeter verifizieren.

Nach jeder Änderung an `calibration.c`: `CHANGELOG.md` aktualisieren, dann
Firmware neu bauen und flashen (die Konstanten sind zur Compile-Zeit fest
verdrahtet, kein EEPROM/Flash-Parameter zur Laufzeit).
