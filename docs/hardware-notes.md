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
