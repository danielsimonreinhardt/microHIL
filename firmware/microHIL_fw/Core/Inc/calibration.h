#ifndef CALIBRATION_H
#define CALIBRATION_H

#include <stdint.h>

/* Stueckweise lineare Kalibrierung ueber N gemessene Wertepaare
 * (x1,y1)...(xN,yN), x aufsteigend sortiert. Zwischen benachbarten Punkten
 * wird linear interpoliert, ausserhalb des Bereichs mit der Steigung des
 * jeweils aussenliegenden Segments extrapoliert. Wird sowohl vorwaerts
 * (Rohwert -> physikalischer Wert bei AIN/CURR, Cal_Apply) als auch
 * rueckwaerts (gewuenschter physikalischer Wert -> anzusteuernder Rohwert bei
 * AOUT, Cal_Invert) verwendet -- Cal_Invert setzt voraus, dass y mit x
 * monoton steigt (gilt fuer alle drei Kanaltypen hier). Ersetzt die
 * fruehere starre 2-Punkt-Gerade: reale Messungen (siehe docs/calibration.md,
 * AIN1/AOUT1 2026-09-09) zeigen bei AOUT1 unterhalb des DAC-Rohwerts 500 und
 * bei AIN1 unterhalb ca. 320mV Rohwert eine deutliche, nicht durch eine
 * einzelne Gerade abbildbare Kruemmung -- vermutlich die dokumentierte
 * Uebernahmeverzerrung des AOUT-Gegentaktpuffers bzw. ADC-Nichtlinearitaet
 * nahe Null bei AIN. Mit mehreren Stuetzstellen je Kanal (aktuell nur fuer
 * AIN1/AOUT1 gemessen, andere Kanaele weiterhin 2 Punkte) wird das
 * abgebildet, statt in genau diesem Bereich systematisch falsch zu liegen.
 * Siehe docs/calibration.md fuer die Messprozedur. */
typedef struct
{
  const int32_t *x;
  const int32_t *y;
  uint8_t n; /* Anzahl Stuetzstellen, mindestens 2 */
} cal_curve_t;

int32_t Cal_Apply(const cal_curve_t *cal, int32_t x);
int32_t Cal_Invert(const cal_curve_t *cal, int32_t y);

extern const cal_curve_t cal_ain[4];
extern const cal_curve_t cal_curr[2];
extern const cal_curve_t cal_aout[2];

#endif /* CALIBRATION_H */
