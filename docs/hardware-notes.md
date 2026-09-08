# Hardware-Notizen

## Bekannte Probleme (aktuelle Platinen-Revision)

### VBUS direkt auf Versorgungsschiene (kein Regler dazwischen)

**Symptom:** Wenn Board gleichzeitig über ST-Link (3.3V) und über den
eigenen USB-Anschluss versorgt wird, steigt die gemessene VDD auf ca.
4.1V statt 3.3V. SWD-Zugriff (ST-Link) schlägt in diesem Zustand fehl
(`Unable to get core ID`).

**Ursache:** Der VBUS-Pin des USB-Steckers ist direkt mit der
Versorgungsschiene verbunden, statt über den Spannungsregler zu laufen.
Bei gleichzeitiger Fremdversorgung (z. B. durch ST-Link) kollidieren die
5V von VBUS mit der 3.3V-Versorgung auf derselben Schiene.

**Workaround (aktuell):** VBUS-Verbindung provisorisch aufgetrennt.

**Für nächste Revision:** VBUS über den Spannungsregler führen (bzw. nur
zur USB-VBUS-Erkennung an einen dedizierten, hochohmig entkoppelten Pin
legen), nicht direkt auf die Versorgungsschiene.

### Digital-Eingänge lesen unbeschaltet immer High

**Symptom:** Wenn an einem Digital-Eingang nichts angeschlossen ist, wird er
immer als High erkannt.

**Ursache:** Externer 10-kΩ-Pullup + LPF vor dem GPIO-Pin. Ein interner
STM32-Pulldown (~40 kΩ typ., siehe Messung beim CAN1_RX-Problem unten) kann
den niederohmigeren externen 10-kΩ-Pullup nicht überstimmen (Teiler ergibt
~0.8×VDD am Pin, klar über VIH) — per Software allein nicht lösbar.

**Fix:** Externer 10-kΩ-Pullup an allen Digital-Inputs entfernt, interner
STM32-Pulldown übernimmt jetzt den definierten Ruhezustand.

### CAN1_RX (PB8) liegt offen — Bus kommt nicht hoch

**Symptom:** Der SLCAN-Layer selbst funktioniert (Protokoll-Parser, `V`/`N`/`F`
antworten korrekt), aber `O`, `L` und auch `Y` (Loopback-Selbsttest, kein Bus
nötig) scheitern alle mit `\a` (BEL). Per Debugger bestätigt: `HAL_CAN_Start()`
läuft in den 10-ms-Timeout (`HAL_CAN_ERROR_TIMEOUT`), weil `INAK` nie löscht —
der bxCAN-Kern wartet beim Verlassen des Init-Modus auf 11 aufeinanderfolgende
rezessive Bits auf der physischen RX-Leitung, **auch im Loopback-Modus**
(dieser Sync-Mechanismus hängt am echten Pin, nicht am internen
Loopback-Pfad — ein bekanntes bxCAN-Verhalten).

**Ursache (per Debugger verifiziert, nicht nur vermutet):** PB8 (CAN1_RX)
liest im Ruhezustand konstant LOW (dominant) statt HIGH (rezessiv). Test: mit
intern zugeschaltetem Pull-Up (`GPIOB->PUPDR`) springt der Pin sofort auf
HIGH, mit Pull-Down oder ganz ohne Pull bleibt er LOW — ein aktiv angesteuerter
Transceiver-Ausgang (Innenwiderstand ~10-100 Ω) würde einen schwachen internen
Pull (~40 kΩ) mühelos überstimmen. Der Pin ist also **nicht aktiv getrieben,
sondern floatet** — es zieht nichts Definiertes dagegen. `CAN1_SILENT` (PB7)
umzuschalten (LOW↔HIGH) hat dabei **keinerlei Auswirkung** auf PB8, was eine
einfache Polaritätsverwechslung beim Silent-Pin als Ursache ausschließt.

**Root Cause (gefunden):** Defekter CAN-Transceiver. Direktes Nachmessen am
RXD-Pin des Transceiver-ICs zeigte ca. 0,2 V statt der erwarteten ~5 V
(rezessiver Ruhepegel) — der Transceiver selbst hat die Leitung nicht
korrekt auf HIGH getrieben, unabhängig von MCU-Seite oder Verdrahtung.
Zwei Zwischenschritte haben das nicht behoben und sind damit als Ursache
ausgeschlossen: kalte Lötstellen (nachgelötet, kein Effekt) und
Silent-Pin-Polarität (siehe oben, PB7-Umschalten ohne Wirkung auf PB8).

**Fix:** Transceiver getauscht. Nach dem Tausch: `O`, `L` und `Y` öffnen
alle sauber (`OK`, keine Statusflags), Loopback-Frames (Standard, Extended,
beide RTR-Varianten, 1 Mbit/s) laufen fehlerfrei durch, Parallelbetrieb mit
dem HIL-Port über 5 Runden ohne Aussetzer. Damit ist CAN1 als CAN-USB-
Interface funktionsfähig.

**Noch offen:** Test gegen einen echten zweiten Busteilnehmer (bisher nur
Loopback/Selbsttest verifiziert) und Bitraten-Messung am Oszilloskop gegen
die berechneten Werte aus `docs/can-usb.md`.

### PWR12-1 liefert 0V trotz `PWR12 1 1` (Firmware meldet "ein")

**Symptom:** `PWR12 1 1` setzt den GPIO (PC4/`DOUT_12V_SUPPLY1`), `PWR12? 1`
liest korrekt "1" zurück (reine GPIO-Rücklesung, keine echte
Ausgangs-Rückmeldung) — am `12V_OUT_1`-Ausgang liegen aber trotz angeschlossener
externer 12V-Einspeisung ~0V an.

**Schaltung (`ELECTRICS/micro_HIL_v0.1`, Sheet `Output-Driver.kicad_sch`,
"12V SWITCHED SUPPLY 1"):** Externe 12V → **Q22** (PMT200EPEX, PMOS,
Verpolschutz) → Zwischenknoten → **Q28** (PMT200EPEX, PMOS, eigentlicher
Schalter) → **R73** (50 mΩ Shunt) → **F13** (Polyfuse, siehe Abschnitt
"Maximal zulässiger Strom" unten) → `12V_OUT_1`. Beide PMOS-Gates werden über
**Q26** (2N7002 NMOS) vom GPIO PC4 (`DOUT_12V_SUPPLY1`) gegen GND gezogen, um
sie durchzuschalten. PMT200EPEX-Gehäuse: **SOT223** (SC-73, 4-Pin mit
Kühlfläche), nicht SOT-23 wie zunächst notiert.

**Diagnose (2026-09-06, per Multimeter):**
- Zwischenknoten Q22/Q28: 0V, obwohl Pin2 (Drain) von Q22 die vollen 12V der
  Einspeisung führt — der Spannungsabfall passiert also bereits an Q22.
- Q22 einzeln gemessen: Pin1 (Gate) 0V, Pin2 (Drain) 12V, Pin3 (Source) 0V.
- Dioden-Test an Q22 (Drain-Source, Versorgung abgesteckt): 1,6V
  Durchlassspannung in einer Richtung, `OL` in der anderen.
- Vergleich mit baugleichen PMT200EPEX auf derselben Platine (z. B. Q28):
  dort nur ~0,568V Durchlassspannung, `OL` in Sperrrichtung — normales
  Verhalten einer intakten Body-Diode.

**Root Cause:** Q22 hat eine defekte Body-Diode (deutlich erhöhte
Durchlassspannung ggü. baugleichen, funktionierenden MOSFETs auf derselben
Platine) und blockiert dadurch den kompletten Strompfad, unabhängig von der
Gate-Ansteuerung über Q26/PC4. Vermutlich Vorschaden (Überstrom/ESD), keine
Fehlbestückung (Bauteil ist verlötet, sieht optisch unauffällig aus).

**PWR12-2 geprüft (2026-09-06):** liefert sauber 12V, Kanal 2 (Q23/Q29) ist
nicht betroffen — der Defekt ist auf Q22/Kanal 1 begrenzt.

**Fix (2026-09-08):** Q22 getauscht. Nach dem Tausch: unbelastet 12V (korrekt),
aber unter Last (0,3A) brach die Ausgangsspannung auf 8,5V ein (~3,3V Abfall
bei 0,3A ⇒ ~11Ω effektiver Serienwiderstand — bei intaktem PMOS+Shunt sollte
das nur ~60mV/~200mΩ sein). Bisektion ergab: am Zwischenknoten Q22/Q28 lagen
unter Last weiterhin 11,9V an (Q22 also gesund), der Abfall passierte also
zwischen dort und `12V_OUT_1` — **Q28 war ebenfalls defekt** (gleicher
Bauteiltyp/vermutlich gleiches Los wie Q22). Nach zusätzlichem Tausch von
Q28: unbelastet 12V, bei 0,3A Last nur noch 11,8V (~0,2V Abfall, normaler
Bereich). Beide MOSFETs (Q22 und Q28) mussten getauscht werden, nicht nur
Q22.

### CURR1/CURR2 liefern nicht-monotone/falsche Rohwerte bei realem Laststrom

**Symptom:** `CURRRAW? <1-2>` sollte laut Schaltungsauslegung ca. 1 mV
Rohwert pro 1 mA Laststrom liefern (INA240A1D, Gain 20 V/V, 50 mΩ Shunt
R73/R74, siehe `docs/calibration.md`). Gemessen an CURR2 (2026-09-06,
PWR12-2 bestätigt korrekt auf 12V, Laststrom mit Amperemeter in Reihe
verifiziert):

| Laststrom (Amperemeter) | `CURRRAW? 2` |
|---|---|
| 200 mA  | 181 |
| 1000 mA | 264 |
| 2000 mA | ~221 (220-224, stabil) |

Nicht nur stark nichtlinear (1000mA→264 vs. 2000mA→~221 wäre bei linearer
Kennlinie unmöglich), sondern **nicht-monoton**: der Rohwert bei 1A ist höher
als bei 2A.

**Schaltung:** `12V_OUT_<n>` → Shunt R73/R74 (50 mΩ) → **U18**/**U19**
(INA240A1D, SOIC-8, Gain 20 V/V, REF1/REF2 auf GND) → `AIN_12V_OUT<n>_CURR`
zum ADC.

**Root Cause (gefunden, 2026-09-06):** **U18 UND U19 sind beide nicht
bestückt** — per Sichtprüfung bestätigt, keine defekten Bauteile. Der
ADC-Eingang liegt dadurch unbeschaltet/floatend und pickt Störsignale auf,
die mit dem Laststrom der nahen Schaltung korrelieren, aber keinen echten
Zusammenhang mit dem tatsächlichen Strom haben — daher die willkürlichen,
nicht-monotonen Werte. Unvollständige Erstbestückung, kein Schaltungs- oder
Firmwarefehler.

**Fix (2026-09-08):** U18 und U19 bestückt. Unbelastet danach beide sauber
bei ~0 (CURR1: 4, CURR2: 0, stabil über mehrere Abfragen) statt der vorher
willkürlichen ~180-260er-Rohwerte. Unter realer Last ebenfalls plausibel
(z. B. CURR1 bei 300mA gemessenem Laststrom → Rohwert 301, siehe
`docs/calibration.md` für die vollständige Kalibrierung).

### Maximal zulässiger Dauerstrom PWR12/CURR (Sicherungen, nicht die MOSFETs)

Die im Schaltplan angegebene Vollausschlag-Formel für CURR
(`Vout = I·50mΩ·20 = 2,5V @ 2,5A`) beschreibt nur die Verstärker-Auslegung,
NICHT den durch die Hardware tatsächlich zulässigen Dauerstrom — der ist
niedriger:

- **F1** (`Input-Output.kicad_sch`, "12V EXTERNAL INPUT PROTECTION"): liegt
  VOR dem Kanal-Split auf dem gemeinsamen `+12V`-Netz, begrenzt also die
  **Summe aus PWR12-1 und PWR12-2 gemeinsam**.
- **F13**/**F14**: je Kanal, nach dem Shunt, kurz vor `12V_OUT_1`/`12V_OUT_2`.
- Alle drei vom Typ **MINISMDC150F/24-2** (Littelfuse PolySwitch PPTC):
  Ihold = 1,5A, Itrip = 3A @ 24V, Trip-Zeit bei 3A ≈ 1,5s.
- Die beiden PMOS je Kanal (PMT200EPEX, SOT223, Rds(on) 130-167mΩ,
  ID(cont)=2,4A@25°C) sind dabei NICHT die begrenzende Komponente: bei 1,5A
  Dauerstrom fallen pro MOSFET nur ca. 1,5²×0,167Ω ≈ 0,38W an, deutlich unter
  dem Ptot-Budget (800mW ohne, 1,75W mit Kupder-Kühlfläche).

**Praktische Grenze:** **≤ 1,5A pro Kanal, wenn nur ein Kanal aktiv ist**;
laufen **beide Kanäle gleichzeitig**, muss ihre **Summe** unter 1,5A bleiben
(F1). Für Kalibrier-/Testpunkte empfiehlt sich mit Sicherheitsmarge ≤ 1,2A,
um F13/F14 nicht unnötig zu erwärmen oder auszulösen (Datenblätter:
[Digi-Key](https://www.digikey.com/en/products/detail/littelfuse-inc/MINISMDC150F-24-2/1113513),
[PMT200EPE](https://assets.nexperia.com/documents/data-sheet/PMT200EPE.pdf)).
