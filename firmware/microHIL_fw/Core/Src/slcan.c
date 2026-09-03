/* SLCAN-/Lawicel-Protokoll (CAN232-kompatibel) auf CAN1.
 *
 * Kommandos sind mit CR terminiert, Antwort ist CR (OK) bzw. BEL (Fehler).
 * Damit laeuft CAN1 direkt unter python-can (interface="slcan") und unter
 * Linux ueber slcand als SocketCAN-Interface.
 *
 * Anders als protocol.c ist der Ausgabepfad hier gepuffert statt blockierend:
 * empfangene Frames kommen unaufgefordert und in Buendeln, ein Busy-Wait pro
 * Frame wuerde den Durchsatz auf ~1000 Frames/s deckeln. */

#include "slcan.h"
#include "can_if.h"
#include "main.h"
#include <string.h>

#define CMD_MAX 32    /* laengstes Kommando: 'T' + 8 ID + 1 DLC + 16 Daten = 26 */
#define RX_RING_SIZE 256
#define TX_RING_SIZE 1024
/* Groesse eines USB-Transfers. Deutlich groesser als ein CDC-Paket (64 B):
 * der Stack zerlegt das selbst, aber pro Transfer faellt nur einmal
 * Protokoll-Overhead an - das ist der Unterschied zwischen ~1000 und
 * ~10000 Frames/s. */
#define TX_CHUNK 256

static volatile uint8_t rx_ring[RX_RING_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

static uint8_t tx_ring[TX_RING_SIZE];
static uint16_t tx_head = 0;
static uint16_t tx_tail = 0;

static char cmd_buf[CMD_MAX];
static uint8_t cmd_len = 0;
static uint8_t cmd_overflow = 0;

static uint8_t timestamps_on = 0;

/* Bitraten der Sn-Kommandos. Achtung: S7 ist im originalen Lawicel-CAN232
 * 800 kbit/s, python-can schickt fuer 750 kbit/s ebenfalls S7. Wir folgen
 * Lawicel; 750k gibt es ueber das Zusatzkommando B (siehe docs/can-usb.md). */
static const uint32_t bitrate_table[10] = {
    10000, 20000, 50000, 100000, 125000,
    250000, 500000, 800000, 1000000, 83333,
};

/* --- USB-Ausgabepuffer ---------------------------------------------------- */

static uint16_t tx_used(void)
{
  return (uint16_t)((tx_head - tx_tail) & (TX_RING_SIZE - 1U));
}

/* Schreibt len Bytes in den Sendering. Passt es nicht komplett, wird gar
 * nichts geschrieben und 0 zurueckgegeben - halbe Zeilen wuerden den Parser
 * auf der Hostseite aus dem Tritt bringen. */
static int tx_write(const uint8_t *data, uint16_t len)
{
  uint16_t i;

  if ((uint16_t)(TX_RING_SIZE - 1U - tx_used()) < len)
  {
    return 0;
  }

  for (i = 0; i < len; i++)
  {
    tx_ring[tx_head] = data[i];
    tx_head = (uint16_t)((tx_head + 1U) & (TX_RING_SIZE - 1U));
  }
  return 1;
}

static void tx_flush(void)
{
  uint8_t chunk[TX_CHUNK];
  uint16_t n = 0;
  uint16_t used = tx_used();

  if (used == 0U)
  {
    return;
  }

  while (n < TX_CHUNK && n < used)
  {
    chunk[n] = tx_ring[(uint16_t)((tx_tail + n) & (TX_RING_SIZE - 1U))];
    n++;
  }

  /* Bei USBD_BUSY bleibt alles im Ring stehen und wird beim naechsten
   * Slcan_Poll() erneut versucht - kein Busy-Wait in der Main-Loop. */
  if (Slcan_UsbTransmit(chunk, n) == 0U)
  {
    tx_tail = (uint16_t)((tx_tail + n) & (TX_RING_SIZE - 1U));
  }
}

static void reply_ok(void)
{
  static const uint8_t cr = '\r';
  (void)tx_write(&cr, 1);
}

static void reply_err(void)
{
  static const uint8_t bel = '\a';
  (void)tx_write(&bel, 1);
}

/* --- Hex-Hilfen ----------------------------------------------------------- */

static const char hex_digits[] = "0123456789ABCDEF";

static int hex_val(char c)
{
  if (c >= '0' && c <= '9') { return c - '0'; }
  if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
  if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
  return -1;
}

/* Liest n Hex-Ziffern ab s. -1 bei ungueltigem Zeichen. */
static int32_t hex_read(const char *s, uint8_t n, uint32_t *out)
{
  uint32_t v = 0;
  uint8_t i;

  for (i = 0; i < n; i++)
  {
    int d = hex_val(s[i]);
    if (d < 0)
    {
      return -1;
    }
    v = (v << 4) | (uint32_t)d;
  }
  *out = v;
  return 0;
}

static uint16_t hex_write(char *dst, uint32_t v, uint8_t digits)
{
  uint8_t i;
  for (i = 0; i < digits; i++)
  {
    dst[digits - 1U - i] = hex_digits[v & 0xFU];
    v >>= 4;
  }
  return digits;
}

/* --- Bit-Timing ----------------------------------------------------------- */

/* Sucht zu einer Wunschbitrate die beste bxCAN-Aufteilung bei PCLK1 = 36 MHz.
 * Kriterium: moeglichst feine Aufloesung (grosses ntq) bei einem Sample Point
 * nahe 87,5 % (ab 800 kbit/s 75 %, dort braucht die Leitungslaufzeit mehr
 * Reserve). Die Bitrate selbst muss auf 0,1 % genau erreichbar sein. */
#define PCLK1_HZ 36000000U
#define SP_TOLERANCE 30U /* Promille Abweichung vom Zielpunkt */

static int timing_for_bitrate(uint32_t bitrate, uint32_t *prescaler,
                              uint32_t *bs1, uint32_t *bs2)
{
  uint32_t target_sp = (bitrate >= 800000U) ? 750U : 875U;
  uint32_t best_err = 0xFFFFFFFFU;
  uint32_t best_ntq = 0, best_brp = 0, best_bs1 = 0, best_bs2 = 0;
  uint32_t ntq;

  if (bitrate < 5000U || bitrate > 1000000U)
  {
    return -1;
  }

  /* ntq von fein nach grob: der erste Treffer innerhalb der Toleranz gewinnt,
   * damit nicht eine grobe Aufteilung mit zufaellig exaktem Sample Point
   * eine feine mit minimal schlechterem verdraengt. */
  for (ntq = 25U; ntq >= 8U; ntq--)
  {
    uint32_t brp = (PCLK1_HZ + (ntq * bitrate) / 2U) / (ntq * bitrate);
    uint32_t actual, diff, t1, t2, sp, err;

    if (brp < 1U || brp > 1024U)
    {
      continue;
    }

    actual = PCLK1_HZ / (brp * ntq);
    diff = (actual > bitrate) ? (actual - bitrate) : (bitrate - actual);
    if (diff * 1000U > bitrate)
    {
      continue; /* mehr als 0,1 % daneben */
    }

    t1 = (ntq * target_sp + 500U) / 1000U;
    if (t1 < 2U) { t1 = 2U; }
    t1 -= 1U;                 /* BS1 ohne das Sync-Segment */
    if (t1 > 16U) { t1 = 16U; }
    t2 = ntq - 1U - t1;
    if (t2 < 1U || t2 > 8U)
    {
      continue;
    }

    sp = ((t1 + 1U) * 1000U) / ntq;
    err = (sp > target_sp) ? (sp - target_sp) : (target_sp - sp);

    if (err < best_err)
    {
      best_err = err;
      best_ntq = ntq;
      best_brp = brp;
      best_bs1 = t1;
      best_bs2 = t2;
    }
    if (err <= SP_TOLERANCE)
    {
      break;
    }
  }

  if (best_ntq == 0U)
  {
    return -1;
  }

  *prescaler = best_brp;
  *bs1 = best_bs1;
  *bs2 = best_bs2;
  return 0;
}

/* --- Frames --------------------------------------------------------------- */

/* Baut die SLCAN-Zeile zu einem empfangenen Frame und legt sie in den
 * Sendering. 0, wenn der Ring voll ist. */
static int emit_frame(const can_frame_t *f)
{
  char out[CMD_MAX + 8];
  uint16_t n = 0;
  uint8_t i;

  if (f->rtr)
  {
    out[n++] = f->ext ? 'R' : 'r';
  }
  else
  {
    out[n++] = f->ext ? 'T' : 't';
  }

  n += hex_write(&out[n], f->id, f->ext ? 8U : 3U);
  out[n++] = hex_digits[f->dlc & 0xFU];

  if (!f->rtr)
  {
    for (i = 0; i < f->dlc; i++)
    {
      n += hex_write(&out[n], f->data[i], 2U);
    }
  }

  if (timestamps_on)
  {
    n += hex_write(&out[n], f->ts_ms, 4U);
  }

  out[n++] = '\r';
  return tx_write((const uint8_t *)out, n);
}

/* Zerlegt t/T/r/R und sendet. -1 bei Formatfehler. */
static int parse_and_send(const char *s, uint8_t len)
{
  can_frame_t f;
  uint32_t v;
  uint8_t id_digits = (s[0] == 'T' || s[0] == 'R') ? 8U : 3U;
  uint8_t need;
  uint8_t i;

  memset(&f, 0, sizeof(f));
  f.ext = (s[0] == 'T' || s[0] == 'R') ? 1U : 0U;
  f.rtr = (s[0] == 'r' || s[0] == 'R') ? 1U : 0U;

  if (len < (uint8_t)(1U + id_digits + 1U))
  {
    return -1;
  }
  if (hex_read(&s[1], id_digits, &v) != 0)
  {
    return -1;
  }
  f.id = v;

  if (hex_val(s[1 + id_digits]) < 0)
  {
    return -1;
  }
  f.dlc = (uint8_t)hex_val(s[1 + id_digits]);
  if (f.dlc > 8U)
  {
    return -1;
  }

  /* RTR-Frames tragen keine Datenbytes, nur den DLC. */
  need = (uint8_t)(1U + id_digits + 1U + (f.rtr ? 0U : (uint8_t)(f.dlc * 2U)));
  if (len != need)
  {
    return -1;
  }

  if (!f.rtr)
  {
    for (i = 0; i < f.dlc; i++)
    {
      if (hex_read(&s[1 + id_digits + 1 + i * 2], 2U, &v) != 0)
      {
        return -1;
      }
      f.data[i] = (uint8_t)v;
    }
  }

  return CanIf_Send(&f);
}

/* --- Kommandodispatch ----------------------------------------------------- */

static void handle_cmd(const char *s, uint8_t len)
{
  uint32_t prescaler, bs1, bs2;
  char out[8];
  uint16_t n;

  if (len == 0U)
  {
    return; /* Leerzeile: CAN232 antwortet nicht */
  }

  switch (s[0])
  {
  case 'S': /* Bitrate ueber Standardindex */
    if (len != 2U || s[1] < '0' || s[1] > '9' || CanIf_IsOpen())
    {
      reply_err();
      return;
    }
    if (timing_for_bitrate(bitrate_table[s[1] - '0'], &prescaler, &bs1, &bs2) != 0 ||
        CanIf_Configure(prescaler, bs1, bs2, 1U) != 0)
    {
      reply_err();
      return;
    }
    reply_ok();
    return;

  case 'B': /* microHIL-Erweiterung: Bitrate dezimal in bit/s, z. B. B750000 */
  {
    uint32_t bitrate = 0;
    uint8_t i;
    if (len < 2U || CanIf_IsOpen())
    {
      reply_err();
      return;
    }
    for (i = 1; i < len; i++)
    {
      if (s[i] < '0' || s[i] > '9')
      {
        reply_err();
        return;
      }
      bitrate = bitrate * 10U + (uint32_t)(s[i] - '0');
      /* Frueh abbrechen: ohne Deckel koennte eine absurd lange Zahl
       * ueberlaufen und zufaellig in einer gueltigen Bitrate landen. */
      if (bitrate > 1000000U)
      {
        reply_err();
        return;
      }
    }
    if (timing_for_bitrate(bitrate, &prescaler, &bs1, &bs2) != 0 ||
        CanIf_Configure(prescaler, bs1, bs2, 1U) != 0)
    {
      reply_err();
      return;
    }
    reply_ok();
    return;
  }

  case 'O': /* Bus normal oeffnen */
  case 'L': /* Bus im Listen-Only-Modus oeffnen */
  case 'Y': /* microHIL-Erweiterung: Loopback fuer den Selbsttest ohne Bus */
    if (len != 1U)
    {
      reply_err();
      return;
    }
    /* Vorher schliessen macht das Oeffnen idempotent. python-can schickt
     * beim Verbinden zweimal 'O' (einmal aus set_bitrate(), einmal aus
     * __init__); ein BEL auf das zweite wuerde nur den Empfangspuffer des
     * Hosts verschmutzen. */
    CanIf_Close();
    if (CanIf_Open(s[0] == 'L' ? CANIF_MODE_LISTEN
                               : (s[0] == 'Y' ? CANIF_MODE_LOOPBACK
                                              : CANIF_MODE_NORMAL)) != 0)
    {
      reply_err();
      return;
    }
    reply_ok();
    return;

  case 'C': /* Bus schliessen. Auch bei bereits geschlossenem Bus OK -
             * python-can schickt das beim Verbinden ungefragt. */
    if (len != 1U)
    {
      reply_err();
      return;
    }
    CanIf_Close();
    reply_ok();
    return;

  case 't':
  case 'T':
  case 'r':
  case 'R':
    if (parse_and_send(s, len) != 0)
    {
      reply_err();
      return;
    }
    /* CAN232 quittiert Standard-Frames mit 'z', Extended mit 'Z'. */
    out[0] = (s[0] == 'T' || s[0] == 'R') ? 'Z' : 'z';
    out[1] = '\r';
    (void)tx_write((const uint8_t *)out, 2);
    return;

  case 'F': /* Statusflags; das Lesen loescht sie */
    if (len != 1U)
    {
      reply_err();
      return;
    }
    out[0] = 'F';
    n = 1U + hex_write(&out[1], CanIf_ReadClearStatus(), 2U);
    out[n++] = '\r';
    (void)tx_write((const uint8_t *)out, n);
    return;

  case 'V': /* Hardware-/Software-Version */
    if (len != 1U)
    {
      reply_err();
      return;
    }
    (void)tx_write((const uint8_t *)"V0100\r", 6);
    return;

  case 'N': /* Seriennummer: 4 Hex aus der MCU-UID */
    if (len != 1U)
    {
      reply_err();
      return;
    }
    out[0] = 'N';
    n = 1U + hex_write(&out[1], HAL_GetUIDw0() & 0xFFFFU, 4U);
    out[n++] = '\r';
    (void)tx_write((const uint8_t *)out, n);
    return;

  case 'Z': /* Zeitstempel an/aus */
    if (len != 2U || (s[1] != '0' && s[1] != '1'))
    {
      reply_err();
      return;
    }
    timestamps_on = (s[1] == '1') ? 1U : 0U;
    reply_ok();
    return;

  default:
    reply_err();
    return;
  }
}

/* --- oeffentliche API ------------------------------------------------------ */

void Slcan_RxChunk(const uint8_t *data, uint32_t len)
{
  uint32_t i;

  for (i = 0; i < len; i++)
  {
    uint16_t next = (uint16_t)((rx_head + 1U) % RX_RING_SIZE);
    if (next != rx_tail)
    {
      rx_ring[rx_head] = data[i];
      rx_head = next;
    }
    /* Ringpuffer voll: Byte verwerfen. Der Host wartet ohnehin auf CR/BEL. */
  }
}

void Slcan_Poll(void)
{
  can_frame_t f;

  /* 1. Kommandos aus dem USB-Empfangsring */
  while (rx_tail != rx_head)
  {
    char c = (char)rx_ring[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1U) % RX_RING_SIZE);

    if (c == '\n')
    {
      continue; /* toleriert CRLF-Hosts */
    }

    if (c == '\r')
    {
      if (cmd_overflow)
      {
        reply_err();
      }
      else
      {
        handle_cmd(cmd_buf, cmd_len);
      }
      cmd_len = 0;
      cmd_overflow = 0;
      continue;
    }

    if (cmd_len < CMD_MAX)
    {
      cmd_buf[cmd_len++] = c;
    }
    else
    {
      cmd_overflow = 1;
    }
  }

  /* 2. Empfangene CAN-Frames ausgeben. Platz wird *vor* dem Abholen geprueft:
   *    CanIf_Recv() nimmt den Frame aus dem CAN-Ring, ein spaeter
   *    fehlschlagendes emit_frame() wuerde ihn verlieren. 32 Byte ist die
   *    laengste moegliche Zeile (T + 8 ID + DLC + 16 Daten + 4 TS + CR). */
  while ((uint16_t)(TX_RING_SIZE - 1U - tx_used()) >= 32U && CanIf_Recv(&f))
  {
    (void)emit_frame(&f);
  }

  /* 3. Sendering Richtung USB leeren */
  tx_flush();
}
