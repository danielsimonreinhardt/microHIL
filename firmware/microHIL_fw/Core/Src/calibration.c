#include "calibration.h"

/* Kalibrierkonstanten fuer AIN1-4, CURR1-2, AOUT1-2. Alle drei real
 * gemessen (siehe docs/calibration.md).
 *
 * x = "naiver" mV-Wert, wie ihn AINRAW?/CURRRAW? liefern bzw. wie er bei
 *     AOUTRAW als DAC-Sollwert (0..3300) angegeben wird (raw*3300/4095, ohne
 *     Kalibrierung).
 * y = tatsaechlicher physikalischer Wert (Multimeter/Amperemeter).
 */

/* AIN1-4 (PA3/PA0/PA1/PA2): Spannungsteiler R127-134 (100k nach Signal,
 * 33k nach GND) vor Unity-Gain-Puffer (TLV6001) -> Skalierung um
 * (100k+33k)/33k = 4.0303. Nominell 0..3300 mV am ADC-Pin entsprechen
 * 0..13300 mV am AIN-Eingang. Gemessen (2026-09-06, Punkte ca. 1,3V/11V):
 * Steigung nah am nominellen Faktor, alle vier Kanaele konsistent. */
const cal_point_t cal_ain[4] = {
    {319, 1308, 2703, 10990},
    {307, 1308, 2703, 10990},
    {319, 1308, 2702, 10990},
    {319, 1308, 2696, 10990},
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
const cal_point_t cal_curr[2] = {
    {301, 300, 1190, 1200},
    {302, 300, 1191, 1200},
};

/* AOUT1-2 (PA5/PA4 = DAC_OUT2/1): nichtinvertierender OPA990-Verstaerker,
 * Gain = 1 + R63/R62 = 1 + 27k/10k = 3.7 (bzw. R79/R78 fuer Kanal 2, gleiche
 * Werte). Ausgang laeuft danach durch einen BC817/BC807-Gegentaktpuffer
 * (Basisstrom-Verstaerkung fuer Lastfaehigkeit), der NICHT in der
 * Gegenkopplungsschleife des OpAmp liegt (Feedback R63/R79 zweigt vor den
 * Transistoren ab) - dadurch moeglicher Offset durch nicht kompensierte
 * Vbe-Abfaelle, der sich erst in der realen Messung zeigt. Nominell 0..3300
 * mV DAC-Sollwert entsprechen 0..12210 mV am Ausgang; gemessen (2026-09-06)
 * liegt die Steigung nah am nominellen Gain 3.7, aber mit einem Offset von
 * ca. -370 mV (Kanal 1) bzw. -380 mV (Kanal 2) - passend zum erwarteten
 * unkompensierten Vbe-Abfall des Gegentaktpuffers. */
const cal_point_t cal_aout[2] = {
    {500, 1477, 2800, 9990},
    {500, 1493, 2800, 10060},
};

int32_t Cal_Apply(const cal_point_t *cal, int32_t x)
{
  int64_t dx = (int64_t)cal->x2 - cal->x1;
  int64_t dy = (int64_t)cal->y2 - cal->y1;
  if (dx == 0)
  {
    return cal->y1;
  }
  int64_t y = (int64_t)cal->y1 + ((int64_t)(x - cal->x1) * dy) / dx;
  return (int32_t)y;
}
