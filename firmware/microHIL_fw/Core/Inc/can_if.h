#ifndef CAN_IF_H
#define CAN_IF_H

#include <stdint.h>

/* Ein CAN-Frame in der Form, die der SLCAN-Layer braucht. */
typedef struct
{
  uint32_t id;       /* 11 bit (ext=0) oder 29 bit (ext=1) */
  uint8_t dlc;       /* 0..8 */
  uint8_t ext;       /* 1 = Extended Frame (29 bit) */
  uint8_t rtr;       /* 1 = Remote Transmission Request */
  uint8_t data[8];
  uint16_t ts_ms;    /* Zeitstempel in ms, laeuft bei 60000 ueber (SLCAN-Format) */
} can_frame_t;

/* SLCAN-Statusflags ('F'-Kommando). Bit 4 ist im CAN232 unbenutzt. */
#define CANIF_FLAG_RX_FULL   0x01U
#define CANIF_FLAG_TX_FULL   0x02U
#define CANIF_FLAG_WARNING   0x04U
#define CANIF_FLAG_OVERRUN   0x08U
#define CANIF_FLAG_PASSIVE   0x20U
#define CANIF_FLAG_ARB_LOST  0x40U
#define CANIF_FLAG_BUS_ERROR 0x80U

/* Bit-Timing setzen. Nur im geschlossenen Zustand erlaubt.
 * prescaler 1..1024, bs1 1..16, bs2 1..8, sjw 1..4 (jeweils in tq).
 * Gibt 0 zurueck bei Erfolg, -1 bei ungueltigen Werten oder offenem Bus. */
int CanIf_Configure(uint32_t prescaler, uint32_t bs1, uint32_t bs2, uint32_t sjw);

/* Bus aufschalten. mode: 0 = normal, 1 = listen only (silent),
 * 2 = loopback (Selbsttest ohne Bus). 0 bei Erfolg, -1 sonst. */
int CanIf_Open(uint8_t mode);
#define CANIF_MODE_NORMAL   0U
#define CANIF_MODE_LISTEN   1U
#define CANIF_MODE_LOOPBACK 2U

/* Bus abschalten. Danach ist wieder CanIf_Configure() erlaubt. */
void CanIf_Close(void);

/* 1, wenn der Bus offen ist. */
int CanIf_IsOpen(void);

/* Frame senden. 0 bei Erfolg, -1 wenn alle Mailboxen belegt sind oder
 * der Bus nicht offen ist. */
int CanIf_Send(const can_frame_t *f);

/* Aeltesten empfangenen Frame holen. 1 = Frame geliefert, 0 = Ring leer. */
int CanIf_Recv(can_frame_t *f);

/* Statusflags lesen; das Lesen loescht sie (wie im CAN232 spezifiziert). */
uint8_t CanIf_ReadClearStatus(void);

#endif /* CAN_IF_H */
