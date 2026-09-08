#ifndef CALIBRATION_H
#define CALIBRATION_H

#include <stdint.h>

/* Lineare 2-Punkt-Kalibrierung: (x1,y1) und (x2,y2) sind zwei gemessene
 * Wertepaare, dazwischen (und leicht darueber hinaus) wird linear
 * interpoliert/extrapoliert. Wird sowohl vorwaerts (Rohwert -> physikalischer
 * Wert bei AIN/CURR) als auch rueckwaerts (gewuenschter physikalischer Wert
 * -> anzusteuernder Rohwert bei AOUT, mit vertauschten x/y) verwendet. Siehe
 * docs/calibration.md fuer die Messprozedur. */
typedef struct
{
  int32_t x1, y1;
  int32_t x2, y2;
} cal_point_t;

int32_t Cal_Apply(const cal_point_t *cal, int32_t x);

extern const cal_point_t cal_ain[4];
extern const cal_point_t cal_curr[2];
extern const cal_point_t cal_aout[2];

#endif /* CALIBRATION_H */
