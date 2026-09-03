/* bxCAN-Treiber fuer CAN1. Kapselt Bit-Timing, Filter, Interrupt-RX und die
 * Fehlerflags, damit slcan.c nur noch Frames ein- und ausgibt.
 *
 * Bewusst nicht ueber CubeMX konfiguriert: SLCAN aendert die Bitrate zur
 * Laufzeit, und die in MX_CAN1_Init() generierten Werte wuerden bei jedem
 * Regenerieren des .ioc zurueckfallen. Dieses Modul macht deshalb
 * HAL_CAN_DeInit() + eigenes CAN_InitTypeDef + HAL_CAN_Init(); die
 * generierten Init-Werte sind irrelevant. Auch NVIC-Freischaltung und
 * IRQ-Handler liegen hier bzw. in USER-CODE-Blocken, nicht im NVIC-Tab. */

#include "can_if.h"
#include "main.h"
#include <string.h>

extern CAN_HandleTypeDef hcan1;

/* Die bxCAN-Hardware-FIFO ist nur 3 Frames tief (~0,2 ms bei 1 Mbit), deshalb
 * muss der RX-Pfad im Interrupt laufen und in einen groesseren Ring puffern. */
#define RX_RING_SIZE 64

static volatile can_frame_t rx_ring[RX_RING_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

static volatile uint8_t status_flags = 0;
static uint8_t bus_open = 0;

/* Default bis zum ersten Sn-Kommando: 500 kbit/s (siehe bitrate_table in slcan.c) */
static uint32_t cfg_prescaler = 4;
static uint32_t cfg_bs1 = 15;
static uint32_t cfg_bs2 = 2;
static uint32_t cfg_sjw = 1;

/* Filterbaenke 0..13 gehoeren CAN1, ab 14 CAN2. bxCAN teilt sich die 28
 * Baenke zwischen beiden Instanzen; ohne diesen Split empfaengt CAN2 spaeter
 * (Fernsteuerung ueber CAN2) grundsaetzlich nichts. */
#define CAN2_START_FILTER_BANK 14U

static uint32_t sjw_reg(uint32_t tq) { return (tq - 1U) << CAN_BTR_SJW_Pos; }
static uint32_t bs1_reg(uint32_t tq) { return (tq - 1U) << CAN_BTR_TS1_Pos; }
static uint32_t bs2_reg(uint32_t tq) { return (tq - 1U) << CAN_BTR_TS2_Pos; }

int CanIf_Configure(uint32_t prescaler, uint32_t bs1, uint32_t bs2, uint32_t sjw)
{
  if (bus_open)
  {
    return -1;
  }
  /* bxCAN: BRP 1..1024, TS1 1..16 tq, TS2 1..8 tq, SJW 1..4 tq */
  if (prescaler < 1U || prescaler > 1024U ||
      bs1 < 1U || bs1 > 16U ||
      bs2 < 1U || bs2 > 8U ||
      sjw < 1U || sjw > 4U)
  {
    return -1;
  }

  cfg_prescaler = prescaler;
  cfg_bs1 = bs1;
  cfg_bs2 = bs2;
  cfg_sjw = sjw;
  return 0;
}

static int can_init(uint8_t mode)
{
  CAN_FilterTypeDef filter = {0};

  (void)HAL_CAN_DeInit(&hcan1);

  hcan1.Instance = CAN1;
  hcan1.Init.Prescaler = cfg_prescaler;
  hcan1.Init.SyncJumpWidth = sjw_reg(cfg_sjw);
  hcan1.Init.TimeSeg1 = bs1_reg(cfg_bs1);
  hcan1.Init.TimeSeg2 = bs2_reg(cfg_bs2);
  hcan1.Init.TimeTriggeredMode = DISABLE;
  /* Automatische Bus-Off-Erholung: ein Interface soll nach einem Busfehler
   * von allein zurueckkommen, ohne dass der Host neu oeffnen muss. */
  hcan1.Init.AutoBusOff = ENABLE;
  hcan1.Init.AutoWakeUp = DISABLE;
  /* CubeMX-Default war DISABLE = One-Shot. Fuer ein CAN-Interface falsch:
   * verlorene Arbitrierung wuerde den Frame stillschweigend verwerfen. */
  hcan1.Init.AutoRetransmission = ENABLE;
  /* Bei vollem FIFO das aelteste Frame ueberschreiben statt das neue zu
   * verwerfen - beim Mitschneiden ist der aktuelle Zustand interessanter. */
  hcan1.Init.ReceiveFifoLocked = DISABLE;
  hcan1.Init.TransmitFifoPriority = ENABLE;

  switch (mode)
  {
  case CANIF_MODE_LISTEN:
    hcan1.Init.Mode = CAN_MODE_SILENT;
    break;
  case CANIF_MODE_LOOPBACK:
    hcan1.Init.Mode = CAN_MODE_SILENT_LOOPBACK;
    break;
  default:
    hcan1.Init.Mode = CAN_MODE_NORMAL;
    break;
  }

  if (HAL_CAN_Init(&hcan1) != HAL_OK)
  {
    return -1;
  }

  /* Accept-all auf FIFO0: Maske 0 laesst jede ID durch. Filtern macht der
   * Host - ein CAN-USB-Interface soll alles sehen. */
  filter.FilterBank = 0;
  filter.FilterMode = CAN_FILTERMODE_IDMASK;
  filter.FilterScale = CAN_FILTERSCALE_32BIT;
  filter.FilterIdHigh = 0x0000;
  filter.FilterIdLow = 0x0000;
  filter.FilterMaskIdHigh = 0x0000;
  filter.FilterMaskIdLow = 0x0000;
  filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
  filter.FilterActivation = CAN_FILTER_ENABLE;
  filter.SlaveStartFilterBank = CAN2_START_FILTER_BANK;

  if (HAL_CAN_ConfigFilter(&hcan1, &filter) != HAL_OK)
  {
    return -1;
  }

  return 0;
}

int CanIf_Open(uint8_t mode)
{
  if (bus_open)
  {
    return -1;
  }

  rx_head = 0;
  rx_tail = 0;
  status_flags = 0;

  if (can_init(mode) != 0)
  {
    return -1;
  }

  if (HAL_CAN_Start(&hcan1) != HAL_OK)
  {
    return -1;
  }

  if (HAL_CAN_ActivateNotification(&hcan1,
                                   CAN_IT_RX_FIFO0_MSG_PENDING |
                                   CAN_IT_RX_FIFO0_OVERRUN |
                                   CAN_IT_ERROR_WARNING |
                                   CAN_IT_ERROR_PASSIVE |
                                   CAN_IT_BUSOFF |
                                   CAN_IT_LAST_ERROR_CODE |
                                   CAN_IT_ERROR) != HAL_OK)
  {
    return -1;
  }

  /* Prioritaet 1: unterhalb von OTG_FS_IRQn (laeuft auf 0,0), damit die
   * CAN-Interrupts den USB-Stack nicht ausbremsen. */
  HAL_NVIC_SetPriority(CAN1_RX0_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);
  HAL_NVIC_SetPriority(CAN1_SCE_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(CAN1_SCE_IRQn);

  bus_open = 1;
  return 0;
}

void CanIf_Close(void)
{
  HAL_NVIC_DisableIRQ(CAN1_RX0_IRQn);
  HAL_NVIC_DisableIRQ(CAN1_SCE_IRQn);
  (void)HAL_CAN_Stop(&hcan1);
  bus_open = 0;
}

int CanIf_IsOpen(void)
{
  return bus_open ? 1 : 0;
}

int CanIf_Send(const can_frame_t *f)
{
  CAN_TxHeaderTypeDef hdr = {0};
  uint32_t mailbox;

  if (!bus_open || f->dlc > 8U)
  {
    return -1;
  }

  if (f->ext)
  {
    hdr.IDE = CAN_ID_EXT;
    hdr.ExtId = f->id & 0x1FFFFFFFU;
  }
  else
  {
    hdr.IDE = CAN_ID_STD;
    hdr.StdId = f->id & 0x7FFU;
  }
  hdr.RTR = f->rtr ? CAN_RTR_REMOTE : CAN_RTR_DATA;
  hdr.DLC = f->dlc;
  hdr.TransmitGlobalTime = DISABLE;

  if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) == 0U)
  {
    status_flags |= CANIF_FLAG_TX_FULL;
    return -1;
  }

  if (HAL_CAN_AddTxMessage(&hcan1, &hdr, (uint8_t *)f->data, &mailbox) != HAL_OK)
  {
    status_flags |= CANIF_FLAG_TX_FULL;
    return -1;
  }

  return 0;
}

int CanIf_Recv(can_frame_t *f)
{
  if (rx_tail == rx_head)
  {
    return 0;
  }

  memcpy(f, (const void *)&rx_ring[rx_tail], sizeof(can_frame_t));
  rx_tail = (uint16_t)((rx_tail + 1U) % RX_RING_SIZE);
  return 1;
}

uint8_t CanIf_ReadClearStatus(void)
{
  uint8_t s = status_flags;
  status_flags = 0;
  return s;
}

/* --- Interrupt-Kontext ---------------------------------------------------- */

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
  CAN_RxHeaderTypeDef hdr;
  uint8_t data[8];
  uint16_t next;

  if (hcan->Instance != CAN1)
  {
    return;
  }

  /* Solange leerraeumen, wie die Hardware-FIFO etwas hat - bei einem Burst
   * kann waehrend der ISR ein weiterer Frame ankommen. */
  while (HAL_CAN_GetRxFifoFillLevel(hcan, CAN_RX_FIFO0) > 0U)
  {
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &hdr, data) != HAL_OK)
    {
      return;
    }

    next = (uint16_t)((rx_head + 1U) % RX_RING_SIZE);
    if (next == rx_tail)
    {
      /* Ring voll: Frame verwerfen und melden. Blockieren ist im
       * Interrupt keine Option. */
      status_flags |= CANIF_FLAG_RX_FULL;
      continue;
    }

    if (hdr.IDE == CAN_ID_EXT)
    {
      rx_ring[rx_head].ext = 1;
      rx_ring[rx_head].id = hdr.ExtId;
    }
    else
    {
      rx_ring[rx_head].ext = 0;
      rx_ring[rx_head].id = hdr.StdId;
    }
    rx_ring[rx_head].rtr = (hdr.RTR == CAN_RTR_REMOTE) ? 1U : 0U;
    rx_ring[rx_head].dlc = (uint8_t)hdr.DLC;
    memcpy((void *)rx_ring[rx_head].data, data, 8);
    /* SLCAN-Zeitstempel: ms-Zaehler, laeuft bei 60000 ueber (0xEA5F max). */
    rx_ring[rx_head].ts_ms = (uint16_t)(HAL_GetTick() % 60000U);

    rx_head = next;
  }
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
{
  uint32_t err;

  if (hcan->Instance != CAN1)
  {
    return;
  }

  err = HAL_CAN_GetError(hcan);

  if (err & HAL_CAN_ERROR_EWG)         { status_flags |= CANIF_FLAG_WARNING; }
  if (err & HAL_CAN_ERROR_EPV)         { status_flags |= CANIF_FLAG_PASSIVE; }
  if (err & HAL_CAN_ERROR_BOF)         { status_flags |= CANIF_FLAG_BUS_ERROR; }
  if (err & HAL_CAN_ERROR_RX_FOV0)     { status_flags |= CANIF_FLAG_OVERRUN; }
  if (err & HAL_CAN_ERROR_RX_FOV1)     { status_flags |= CANIF_FLAG_OVERRUN; }
  if (err & (HAL_CAN_ERROR_TX_ALST0 |
             HAL_CAN_ERROR_TX_ALST1 |
             HAL_CAN_ERROR_TX_ALST2))  { status_flags |= CANIF_FLAG_ARB_LOST; }
  if (err & (HAL_CAN_ERROR_STF | HAL_CAN_ERROR_FOR | HAL_CAN_ERROR_ACK |
             HAL_CAN_ERROR_BR | HAL_CAN_ERROR_BD | HAL_CAN_ERROR_CRC |
             HAL_CAN_ERROR_TX_TERR0 | HAL_CAN_ERROR_TX_TERR1 |
             HAL_CAN_ERROR_TX_TERR2)) { status_flags |= CANIF_FLAG_BUS_ERROR; }

  /* Fehlerbits im Handle zuruecksetzen, sonst meldet die HAL sie dauerhaft. */
  hcan->ErrorCode = HAL_CAN_ERROR_NONE;
}
