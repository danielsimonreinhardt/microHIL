#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/* Von CDC_Receive_FS (USB-IRQ-Kontext) aufgerufen: kopiert empfangene Bytes
 * in den Ringpuffer, blockiert nicht. */
void Protocol_RxChunk(const uint8_t *data, uint32_t len);

/* Aus der Main-Loop aufrufen: verarbeitet vollstaendige Zeilen aus dem
 * Ringpuffer und sendet die Antwort. */
void Protocol_Poll(void);

#endif /* PROTOCOL_H */
