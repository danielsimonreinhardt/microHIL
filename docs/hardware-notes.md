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
