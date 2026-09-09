# Kalibrierung: AIN, AOUT, CURR

`AIN?`, `AOUT` und `CURR?` liefern/erwarten physikalische Werte (mV bzw. mA),
keine rohen ADC-/DAC-Werte. Die Umrechnung passiert über eine stückweise
lineare Kalibrierung je Kanal in `Core/Src/calibration.c`
(`cal_ain[4]`, `cal_curr[2]`, `cal_aout[2]`). Jeder Eintrag ist eine
`cal_curve_t` aus N gemessenen Wertepaaren (x aufsteigend sortiert),
zwischen benachbarten Punkten wird linear interpoliert, an den Enden mit der
Steigung des jeweils äußeren Segments extrapoliert (`Cal_Apply()`/
`Cal_Invert()` in `calibration.c`). CURR1-2 haben weiterhin nur die
ursprünglichen 2 Endpunkte; AIN1-4/AOUT1-2 haben seit 2026-09-09 zusätzliche
Zwischenpunkte (siehe unten).

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

**Update 2026-09-09: AOUT1/AIN1 hatten unterhalb des jeweils unteren
Kalibrierpunkts (Rohwert 500 bzw. ca. 320mV) eine reale, nicht durch eine
einzelne Gerade abbildbare Nichtlinearität** — aufgefallen bei einem
Testaufbau AOUT1→AIN1 mit Multimeter am gemeinsamen Knoten: `AOUT 1 1000`
lieferte real nur 906mV (statt ~1000mV), `AIN? 1` dabei sogar nur 687mV
(statt der real anliegenden ~909mV). Ursache laut Zwischenmessungen
(AOUTRAW-Sweep 100/250/372/500, jeweils mit Multimeter/`AINRAW?`
gegengemessen): die Steigung (real/roh) steigt bei AOUT1 stetig von ~2,8
(Rohwert 100) auf ~4,4 (Rohwert 500) — klassische Übernahmeverzerrung des
AOUT-Gegentaktpuffers nahe der Nulldurchgangszone (siehe
"Schaltungs-Herleitung" unten). Bei AIN1 ist der Bereich unterhalb
`AINRAW?`≈27 (real <500mV) noch deutlicher gekrümmt. Behoben durch
Erweiterung von `Cal_Apply`/`cal_point_t` auf stückweise lineare
Interpolation mit mehreren Stützstellen (`cal_curve_t`, siehe oben) statt
einer starren 2-Punkt-Geraden — AIN1/AOUT1 haben jetzt zusätzliche
Messpunkte bei Rohwert 100/250/372 (AOUT) bzw. `AINRAW?` 9/27/166 (AIN),
alle mit Multimeter verifiziert. **Auf Hardware verifiziert:** `AOUT 1 1000`
liefert jetzt real 982mV, `AIN? 1` dabei 984mV (vorher 687mV) — < 0,3%
Abweichung vom real gemessenen Wert.

**AOUT2/AIN2 gleichermassen betroffen und mit derselben Methode behoben**
(2026-09-09, AOUT2→AIN2, Multimeter am gemeinsamen Knoten): vor dem Fix
lieferte `AIN? 2` bei `AOUT 2 1000` nur 678mV (real anliegend: 913mV).
Zwischenpunkte bei Rohwert 100/250/368 (AOUT2, real 148/516/913mV) bzw.
`AINRAW?` 1/26/151 (AIN2, gleiche real-Werte) ergänzt. **Auf Hardware
verifiziert:** `AOUT 2 1000` liefert jetzt real 978mV, `AIN? 2` dabei 973mV
(vorher 678mV) — < 0,6% Abweichung.

**AIN3/AIN4 ebenfalls nachgemessen und behoben** (2026-09-09, Testaufbau
umgesteckt: AOUT1→AIN3, AOUT2→AIN4, Multimeter jeweils am gemeinsamen
Knoten). Vor dem Fix: `AIN? 3` bei `AOUT 1 1000` nur 805mV (real 981mV),
`AIN? 4` bei `AOUT 2 1000` nur 812mV (real 976mV) — derselbe Effekt wie bei
AIN1/AIN2, unabhängig vom jeweils angeschlossenen AOUT-Kanal, also eine
Eigenschaft der AIN-Eingänge selbst (ADC-Nichtlinearität nahe 0), nicht der
AOUT-Ausgänge. Zwischenpunkte bei `AINRAW?` 8/27/195 (AIN3, real
83/502/981mV) bzw. 1/32/197 (AIN4, real 147/509/976mV) ergänzt. **Auf
Hardware verifiziert:** `AIN? 3` zeigt jetzt 981mV (real 981mV, exakt),
`AIN? 4` zeigt 973mV (real 976mV, < 0,4% Abweichung).

Damit haben alle vier AIN-Kanäle und beide AOUT-Kanäle die zusätzlichen
Zwischenpunkte; nur CURR1-2 sind von dieser Änderung nicht betroffen (dort
liegen keine neuen Messpunkte vor, die ursprüngliche 2-Punkt-Kalibrierung
von 2026-09-08 gilt weiter unverändert).

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

Für jeden Kanal mindestens zwei Referenzpunkte messen, die den Nutzbereich
möglichst weit aufspannen (Faustregel: ca. 10 % und 80–90 % des
Vollausschlags — zu eng beieinander liegende Punkte verstärken den Effekt
von Messrauschen auf die berechnete Steigung). Seit der Umstellung auf
`cal_curve_t` (2026-09-09, stückweise linear) können es auch mehr als 2
Punkte sein — sinnvoll, wenn (wie bei AOUT1/AIN1 unterhalb ihres jeweils
unteren Endpunkts) eine einzelne Gerade den tatsächlichen Bereich, in dem
der Kanal genutzt wird, nicht gut genug trifft. Dafür in dem
Zielspannungs-/Zielstrombereich, der tatsächlich gebraucht wird, 1-2
zusätzliche Punkte messen (z. B. per `AOUTRAW`/`AINRAW?`-Sweep) und in die
`x[]`/`y[]`-Arrays der Kurve einsortieren (x aufsteigend).

### AIN1-4

1. Bekannte, präzise Spannung `V1` an AIN `n` anlegen (kalibrierte
   Spannungsquelle, mit Multimeter am Eingang nachgemessen).
2. `AINRAW? n` abfragen → `x1`. Multimeter-Wert (in mV) → `y1`.
3. Zweite Spannung `V2` anlegen, `AINRAW? n` → `x2`, Multimeter → `y2`.
4. In `Core/Src/calibration.c`: die `ainN_x`/`ainN_y`-Arrays für Kanal `n`
   mit `{x1, x2}`/`{y1, y2}` füllen (bzw. weitere Punkte ergänzen).
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
3. In `calibration.c`: die `aoutN_x`/`aoutN_y`-Arrays für Kanal `n` mit
   `{x1, x2}`/`{y1, y2}` füllen (bzw. weitere Punkte ergänzen).
4. Neu bauen/flashen, mit `AOUT n <ziel_mV>` + Multimeter verifizieren.

Nach jeder Änderung an `calibration.c`: `CHANGELOG.md` aktualisieren, dann
Firmware neu bauen und flashen (die Konstanten sind zur Compile-Zeit fest
verdrahtet, kein EEPROM/Flash-Parameter zur Laufzeit).
