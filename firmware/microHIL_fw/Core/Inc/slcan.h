#ifndef SLCAN_H
#define SLCAN_H

#include <stdint.h>

/* SLCAN-/Lawicel-Layer fuer CAN1. Die API spiegelt bewusst protocol.h, damit
 * sie sowohl am gemeinsamen CDC-Port als auch am zweiten CDC-Port des
 * Composite-Geraets haengen kann. */

/* Aus dem USB-IRQ-Kontext aufgerufen, blockiert nicht. */
void Slcan_RxChunk(const uint8_t *data, uint32_t len);

/* Aus der Main-Loop: wertet Kommandos aus, schiebt empfangene CAN-Frames in
 * den Sendepuffer und leert diesen Richtung USB. */
void Slcan_Poll(void);

/* Vom SLCAN-Layer benutzte USB-Ausgabe. Liegt in usbd_cdc_if.c, damit hier
 * nur ein Symbol umgehaengt werden muss, wenn der CAN-Verkehr auf den
 * zweiten CDC-Port wandert. Rueckgabe wie CDC_Transmit_FS: 0 = OK,
 * 1 = busy/Fehler. */
uint8_t Slcan_UsbTransmit(uint8_t *buf, uint16_t len);

#endif /* SLCAN_H */
