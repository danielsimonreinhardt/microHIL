#include "calibration.h"

/* Kalibrierkonstanten fuer AIN1-4, CURR1-2, AOUT1-2. Alle drei real
 * gemessen (siehe docs/calibration.md).
 *
 * x = "naiver" mV-Wert, wie ihn AINRAW?/CURRRAW? liefern bzw. wie er bei
 *     AOUTRAW als DAC-Sollwert (0..3300) angegeben wird (raw*3300/4095, ohne
 *     Kalibrierung).
 * y = tatsaechlicher physikalischer Wert (Multimeter/Amperemeter).
 *
 * Je Kanal eine oder mehrere Stuetzstellen (stueckweise linear, siehe
 * Cal_Apply/Cal_Invert in calibration.h) -- die meisten Kanaele haben nur
 * die urspruenglichen 2 Endpunkte, AIN1/AOUT1 zusaetzlich Zwischenpunkte
 * (siehe unten, "2026-09-09").
 */

/* AIN1-4 (PA3/PA0/PA1/PA2): Spannungsteiler R127-134 (100k nach Signal,
 * 33k nach GND) vor Unity-Gain-Puffer (TLV6001) -> Skalierung um
 * (100k+33k)/33k = 4.0303. Nominell 0..3300 mV am ADC-Pin entsprechen
 * 0..13300 mV am AIN-Eingang. Endpunkte gemessen (2026-09-06, Punkte ca.
 * 1,3V/11V): Steigung nah am nominellen Faktor, alle vier Kanaele
 * konsistent.
 *
 * AIN1 zusaetzlich mit 3 Zwischenpunkten (2026-09-09, AOUT1 direkt auf
 * AIN1 gespeist, Multimeter am gemeinsamen Knoten, siehe docs/
 * calibration.md): unterhalb von x=319 weicht die Kennlinie deutlich von
 * der bis dahin verwendeten 2-Punkt-Geraden ab (bei x=166 sagte die alte
 * Gerade 687mV vorher, real gemessen 909mV) -- mit den Zwischenpunkten
 * jetzt abgebildet statt extrapoliert. */
static const int32_t ain1_x[] = {9, 27, 166, 319, 2703};
static const int32_t ain1_y[] = {84, 500, 909, 1308, 10990};
/* AIN2 mit denselben 3 Zwischenpunkten wie AIN1 nachgemessen (2026-09-09,
 * AOUT2 direkt auf AIN2, siehe docs/calibration.md): dieselbe Kruemmung
 * unterhalb x=307 wie bei AIN1 unterhalb x=319. */
static const int32_t ain2_x[] = {1, 26, 151, 307, 2703};
static const int32_t ain2_y[] = {148, 516, 913, 1308, 10990};
/* AIN3/AIN4 ebenfalls mit denselben 3 Zwischenpunkten nachgemessen
 * (2026-09-09, AOUT1 auf AIN3 bzw. AOUT2 auf AIN4, siehe docs/
 * calibration.md): dieselbe Kruemmung wie AIN1/AIN2, alle vier Kanaele
 * jetzt konsistent kalibriert. */
static const int32_t ain3_x[] = {8, 27, 195, 319, 2702};
static const int32_t ain3_y[] = {83, 502, 981, 1308, 10990};
static const int32_t ain4_x[] = {1, 32, 197, 319, 2696};
static const int32_t ain4_y[] = {147, 509, 976, 1308, 10990};

const cal_curve_t cal_ain[4] = {
    {ain1_x, ain1_y, 5},
    {ain2_x, ain2_y, 5},
    {ain3_x, ain3_y, 5},
    {ain4_x, ain4_y, 5},
};

/* CURR1-2 (PA6/PA7): Shunt R73/R74 = 50 mOhm (LVK24R050CER), INA240A1D
 * (U18/U19) Gain 20 V/V, REF1/REF2 auf GND -> V_sense[mV] = I[mA] * 0.05 * 20
 * = I[mA]. Nominell also 1:1 (1 mV Sense-Spannung = 1 mA Laststrom).
 * Gemessen (2026-09-08, Punkte 0,3A/1,2A, Amperemeter in Reihe): beide
 * Kanaele sehr nah am nominellen 1:1, praktisch identisch zueinander.
 * WICHTIG: max. zulaessiger Dauerstrom ist durch die Polyfuses F1
 * (gemeinsam fuer PWR12-1+2)/F13/F14 (je Kanal) auf 1,5A begrenzt, nicht
 * die schaltplan-abgeleiteten 2,5A Vollausschlag - siehe
 * docs/hardware-notes.md, "Maximal zulaessiger Dauerstrom". */
static const int32_t curr1_x[] = {301, 1190};
static const int32_t curr1_y[] = {300, 1200};
static const int32_t curr2_x[] = {302, 1191};
static const int32_t curr2_y[] = {300, 1200};

const cal_curve_t cal_curr[2] = {
    {curr1_x, curr1_y, 2},
    {curr2_x, curr2_y, 2},
};

/* AOUT1-2 (PA5/PA4 = DAC_OUT2/1): nichtinvertierender OPA990-Verstaerker,
 * Gain = 1 + R63/R62 = 1 + 27k/10k = 3.7 (bzw. R79/R78 fuer Kanal 2, gleiche
 * Werte). Ausgang laeuft danach durch einen BC817/BC807-Gegentaktpuffer
 * (Basisstrom-Verstaerkung fuer Lastfaehigkeit), der NICHT in der
 * Gegenkopplungsschleife des OpAmp liegt (Feedback R63/R79 zweigt vor den
 * Transistoren ab) - dadurch moeglicher Offset durch nicht kompensierte
 * Vbe-Abfaelle, der sich erst in der realen Messung zeigt. Nominell 0..3300
 * mV DAC-Sollwert entsprechen 0..12210 mV am Ausgang.
 *
 * AOUT1/AOUT2 zusaetzlich mit 3 Zwischenpunkten (2026-09-09, siehe docs/
 * calibration.md): die Kruemmung unterhalb von Rohwert 500 ist deutlich
 * staerker als eine einzelne Gerade durch die beiden Endpunkte hergeben
 * wuerde (Steigung real/roh steigt stetig, statt konstant ~3.7) --
 * klassische Uebernahmeverzerrung eines Gegentakt-Ausgangstreibers nahe der
 * Nulldurchgangszone, an beiden Kanaelen gleichermassen. Mit den
 * Zwischenpunkten jetzt abgebildet. */
static const int32_t aout1_x[] = {100, 250, 372, 500, 2800};
static const int32_t aout1_y[] = {84, 500, 909, 1477, 9990};
static const int32_t aout2_x[] = {100, 250, 368, 500, 2800};
static const int32_t aout2_y[] = {148, 516, 913, 1493, 10060};

const cal_curve_t cal_aout[2] = {
    {aout1_x, aout1_y, 5},
    {aout2_x, aout2_y, 5},
};

static int32_t lerp(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x)
{
  int64_t dx = (int64_t)x2 - x1;
  int64_t dy = (int64_t)y2 - y1;
  if (dx == 0)
  {
    return y1;
  }
  int64_t y = (int64_t)y1 + ((int64_t)(x - x1) * dy) / dx;
  return (int32_t)y;
}

int32_t Cal_Apply(const cal_curve_t *cal, int32_t x)
{
  if (x <= cal->x[0])
  {
    return lerp(cal->x[0], cal->y[0], cal->x[1], cal->y[1], x);
  }
  for (uint8_t i = 0; i + 1 < cal->n; i++)
  {
    if (x <= cal->x[i + 1])
    {
      return lerp(cal->x[i], cal->y[i], cal->x[i + 1], cal->y[i + 1], x);
    }
  }
  uint8_t last = cal->n - 1;
  return lerp(cal->x[last - 1], cal->y[last - 1], cal->x[last], cal->y[last], x);
}

/* Kehrfunktion von Cal_Apply (y -> x statt x -> y), fuer AOUT: dort ist der
 * gewuenschte physikalische Wert bekannt (y), gesucht ist der anzusteuernde
 * Rohwert (x). Setzt voraus, dass y mit x monoton steigt (gilt fuer alle
 * drei Kanaltypen hier) -- sucht deshalb das Segment ueber y statt ueber x
 * und interpoliert dort mit vertauschten Rollen. */
int32_t Cal_Invert(const cal_curve_t *cal, int32_t y)
{
  if (y <= cal->y[0])
  {
    return lerp(cal->y[0], cal->x[0], cal->y[1], cal->x[1], y);
  }
  for (uint8_t i = 0; i + 1 < cal->n; i++)
  {
    if (y <= cal->y[i + 1])
    {
      return lerp(cal->y[i], cal->x[i], cal->y[i + 1], cal->x[i + 1], y);
    }
  }
  uint8_t last = cal->n - 1;
  return lerp(cal->y[last - 1], cal->x[last - 1], cal->y[last], cal->x[last], y);
}
