#include "protocol.h"
#include "calibration.h"
#include "main.h"
#include "usbd_cdc_if.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern ADC_HandleTypeDef hadc1;
extern DAC_HandleTypeDef hdac;
extern TIM_HandleTypeDef htim3;

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
} gpio_t;

static const gpio_t relay_gpio[4] = {
    {DOUT_RELAY1_GPIO_Port, DOUT_RELAY1_Pin},
    {DOUT_RELAY2_GPIO_Port, DOUT_RELAY2_Pin},
    {DOUT_RELAY3_GPIO_Port, DOUT_RELAY3_Pin},
    {DOUT_RELAY4_GPIO_Port, DOUT_RELAY4_Pin},
};

static const gpio_t out_gpio[8] = {
    {DOUT_OUT1_GPIO_Port, DOUT_OUT1_Pin},
    {DOUT_OUT2_GPIO_Port, DOUT_OUT2_Pin},
    {DOUT_OUT3_GPIO_Port, DOUT_OUT3_Pin},
    {DOUT_OUT4_GPIO_Port, DOUT_OUT4_Pin},
    {DOUT_OUT5_GPIO_Port, DOUT_OUT5_Pin},
    {DOUT_OUT6_GPIO_Port, DOUT_OUT6_Pin},
    {DOUT_OUT7_GPIO_Port, DOUT_OUT7_Pin},
    {DOUT_OUT8_GPIO_Port, DOUT_OUT8_Pin},
};

static const gpio_t in_gpio[8] = {
    {DIN_IN1_GPIO_Port, DIN_IN1_Pin},
    {DIN_IN2_GPIO_Port, DIN_IN2_Pin},
    {DIN_IN3_GPIO_Port, DIN_IN3_Pin},
    {DIN_IN4_GPIO_Port, DIN_IN4_Pin},
    {DIN_IN5_GPIO_Port, DIN_IN5_Pin},
    {DIN_IN6_GPIO_Port, DIN_IN6_Pin},
    {DIN_IN7_GPIO_Port, DIN_IN7_Pin},
    {DIN_IN8_GPIO_Port, DIN_IN8_Pin},
};

static const gpio_t pwr12_gpio[2] = {
    {DOUT_12VOUT1_GPIO_Port, DOUT_12VOUT1_Pin},
    {DOUT_12VOUT2_GPIO_Port, DOUT_12VOUT2_Pin},
};

#define PWR12_LIMIT_DEFAULT_MA  1200
#define PWR12_TOTAL_BUDGET_MA   1500
#define PWR12_OC_DEBOUNCE_MS    100U
#define PWR12_SAMPLE_PERIOD_MS  2U

/* Software-Strombegrenzung fuer PWR12-1/2 (siehe docs/protocol.md,
 * Abschnitt "Strombegrenzung"). requested_on ist der zuletzt vom Host per
 * PWR12 gesetzte Sollzustand, unabhaengig vom tatsaechlich geschalteten
 * GPIO - eine Verriegelung (oc_latched oder pwr12_budget_latched) haelt
 * den physischen Ausgang aus, obwohl requested_on weiterhin 1 ist, bis der
 * Host die Schaltanforderung einmal per PWR12 <n> 0 zurueckgenommen hat. */
typedef struct
{
  int32_t limit_ma;
  uint8_t requested_on;
  uint8_t oc_latched;
  uint8_t over_active;
  uint32_t over_since;
} pwr12_ch_t;

static pwr12_ch_t pwr12_ch[2] = {
    {PWR12_LIMIT_DEFAULT_MA, 0, 0, 0, 0},
    {PWR12_LIMIT_DEFAULT_MA, 0, 0, 0, 0},
};
/* Verriegelt sofort (keine Entprellung) beide Kanaele, wenn CURR1+CURR2 in
 * Summe das gemeinsame Eingangsbudget ueberschreiten - der Eingang versorgt
 * neben PWR12-1/2 auch die MCU. Loest sich erst, wenn fuer BEIDE Kanaele
 * die Schaltanforderung zurueckgenommen wurde. */
static uint8_t pwr12_budget_latched = 0;
static uint32_t pwr12_last_sample_tick = 0;

/* AIN1..4 -> PA3,PA0,PA1,PA2 (siehe microHIL_fw.ioc) */
static const uint32_t ain_channel[4] = {ADC_CHANNEL_3, ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_2};
/* CURR1..2 -> PA6,PA7 (12V-Ausgang Stromsense) */
static const uint32_t curr_channel[2] = {ADC_CHANNEL_6, ADC_CHANNEL_7};
/* AOUT1 -> PA5/DAC_CHANNEL_2, AOUT2 -> PA4/DAC_CHANNEL_1 (siehe microHIL_fw.ioc) */
static const uint32_t dac_channel[2] = {DAC_CHANNEL_2, DAC_CHANNEL_1};
/* PWM1..4 -> PC6..PC9 (TIM3 CH1..4). Treiben laut Schaltplan dieselbe
 * Endstufe wie OUT1..4 -> softwareseitige Verriegelung in cmd_out_set/pwm_set. */
static const uint32_t pwm_tim_channel[4] = {TIM_CHANNEL_1, TIM_CHANNEL_2, TIM_CHANNEL_3, TIM_CHANNEL_4};
#define TIM3_ARR 65535U

#define RX_RING_SIZE 256
#define LINE_MAX 64

static volatile uint8_t rx_ring[RX_RING_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

static char line_buf[LINE_MAX];
static uint16_t line_len = 0;

void Protocol_RxChunk(const uint8_t *data, uint32_t len)
{
  for (uint32_t i = 0; i < len; i++)
  {
    uint16_t next = (uint16_t)((rx_head + 1) % RX_RING_SIZE);
    if (next != rx_tail)
    {
      rx_ring[rx_head] = data[i];
      rx_head = next;
    }
    /* Ringpuffer voll: Byte wird verworfen, Host muss ohnehin auf OK/ERR warten */
  }
}

static void send_line(const char *s)
{
  uint32_t len = (uint32_t)strlen(s);
  uint32_t start = HAL_GetTick();
  while (CDC_Transmit_FS((uint8_t *)s, (uint16_t)len) == USBD_BUSY)
  {
    if ((HAL_GetTick() - start) > 100)
    {
      return;
    }
  }
}

static void reply_ok(void)
{
  send_line("OK\r\n");
}

static void reply_err(const char *why)
{
  char buf[32];
  snprintf(buf, sizeof(buf), "ERR %s\r\n", why);
  send_line(buf);
}

static void reply_val(int32_t v)
{
  char buf[16];
  snprintf(buf, sizeof(buf), "%ld\r\n", (long)v);
  send_line(buf);
}

/* Liest einen ADC-Kanal einmalig (Single Conversion) und rechnet in mV um. */
static int32_t adc_read_mv(uint32_t channel)
{
  ADC_ChannelConfTypeDef sConfig = {0};
  sConfig.Channel = channel;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_15CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    return -1;
  }

  HAL_ADC_Start(&hadc1);
  if (HAL_ADC_PollForConversion(&hadc1, 10) != HAL_OK)
  {
    HAL_ADC_Stop(&hadc1);
    return -1;
  }
  uint32_t raw = HAL_ADC_GetValue(&hadc1);
  HAL_ADC_Stop(&hadc1);

  return (int32_t)((raw * 3300UL) / 4095UL);
}

static void pwr12_apply(int idx)
{
  int on = pwr12_ch[idx].requested_on && !pwr12_ch[idx].oc_latched && !pwr12_budget_latched;
  HAL_GPIO_WritePin(pwr12_gpio[idx].port, pwr12_gpio[idx].pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* Aus Protocol_Poll() bei jedem Aufruf angestossen, sampled aber nur alle
 * PWR12_SAMPLE_PERIOD_MS: misst CURR1/2, prueft das gemeinsame Eingangs-
 * budget (sofort, keine Entprellung) und je Kanal das eigene Limit
 * (entprellt ueber PWR12_OC_DEBOUNCE_MS, um kurze Einschaltstromspitzen zu
 * tolerieren) und schaltet die Ausgaenge entsprechend. */
static void pwr12_guard_poll(void)
{
  uint32_t now = HAL_GetTick();
  if ((now - pwr12_last_sample_tick) < PWR12_SAMPLE_PERIOD_MS)
  {
    return;
  }
  pwr12_last_sample_tick = now;

  int32_t ma[2];
  for (int i = 0; i < 2; i++)
  {
    int32_t naive_mv = adc_read_mv(curr_channel[i]);
    ma[i] = (naive_mv < 0) ? 0 : Cal_Apply(&cal_curr[i], naive_mv);
    if (ma[i] < 0)
    {
      ma[i] = 0;
    }
  }

  if (!pwr12_budget_latched && (ma[0] + ma[1]) > PWR12_TOTAL_BUDGET_MA)
  {
    pwr12_budget_latched = 1;
  }

  for (int i = 0; i < 2; i++)
  {
    if (ma[i] > pwr12_ch[i].limit_ma)
    {
      if (!pwr12_ch[i].over_active)
      {
        pwr12_ch[i].over_active = 1;
        pwr12_ch[i].over_since = now;
      }
      else if ((now - pwr12_ch[i].over_since) >= PWR12_OC_DEBOUNCE_MS)
      {
        pwr12_ch[i].oc_latched = 1;
      }
    }
    else
    {
      pwr12_ch[i].over_active = 0;
    }
  }

  pwr12_apply(0);
  pwr12_apply(1);
}

static void dac_write_mv(uint32_t channel, int32_t mv)
{
  if (mv < 0) mv = 0;
  if (mv > 3300) mv = 3300;
  uint32_t raw = (uint32_t)(((uint32_t)mv * 4095UL) / 3300UL);
  HAL_DAC_SetValue(&hdac, channel, DAC_ALIGN_12B_R, raw);
  HAL_DAC_Start(&hdac, channel);
}

/* Schaltet PWM-Kanal idx (0-basiert) auf permille (0-1000) Duty Cycle.
 * permille > 0 schaltet zwangsweise den Konflikt-OUT desselben Kanals ab. */
static void pwm_set(int idx, int permille)
{
  if (permille < 0) permille = 0;
  if (permille > 1000) permille = 1000;

  if (permille > 0)
  {
    HAL_GPIO_WritePin(out_gpio[idx].port, out_gpio[idx].pin, GPIO_PIN_RESET);
  }

  uint32_t compare = ((uint32_t)permille * TIM3_ARR) / 1000U;
  __HAL_TIM_SET_COMPARE(&htim3, pwm_tim_channel[idx], compare);

  if (permille > 0)
  {
    HAL_TIM_PWM_Start(&htim3, pwm_tim_channel[idx]);
  }
  else
  {
    HAL_TIM_PWM_Stop(&htim3, pwm_tim_channel[idx]);
  }
}

/* OUT 1-4 schaltet bei "an" zwangsweise den Konflikt-PWM-Kanal ab (siehe pwm_set). */
static void cmd_out_set(void)
{
  char *a1 = strtok(NULL, " \t");
  char *a2 = strtok(NULL, " \t");
  if (!a1 || !a2)
  {
    reply_err("ARGS");
    return;
  }
  int idx = atoi(a1);
  if (idx < 1 || idx > 8)
  {
    reply_err("RANGE");
    return;
  }
  int on = atoi(a2) ? 1 : 0;

  if (on && idx <= 4)
  {
    __HAL_TIM_SET_COMPARE(&htim3, pwm_tim_channel[idx - 1], 0);
    HAL_TIM_PWM_Stop(&htim3, pwm_tim_channel[idx - 1]);
  }

  HAL_GPIO_WritePin(out_gpio[idx - 1].port, out_gpio[idx - 1].pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
  reply_ok();
}

static void cmd_pwm(void)
{
  char *a1 = strtok(NULL, " \t");
  char *a2 = strtok(NULL, " \t");
  if (!a1 || !a2)
  {
    reply_err("ARGS");
    return;
  }
  int idx = atoi(a1);
  if (idx < 1 || idx > 4)
  {
    reply_err("RANGE");
    return;
  }
  pwm_set(idx - 1, atoi(a2));
  reply_ok();
}

static void cmd_pwm_query(void)
{
  char *a1 = strtok(NULL, " \t");
  if (!a1)
  {
    reply_err("ARGS");
    return;
  }
  int idx = atoi(a1);
  if (idx < 1 || idx > 4)
  {
    reply_err("RANGE");
    return;
  }
  uint32_t compare = __HAL_TIM_GET_COMPARE(&htim3, pwm_tim_channel[idx - 1]);
  reply_val((int32_t)((compare * 1000UL) / TIM3_ARR));
}

static void digital_set(const gpio_t *table, int count)
{
  char *a1 = strtok(NULL, " \t");
  char *a2 = strtok(NULL, " \t");
  if (!a1 || !a2)
  {
    reply_err("ARGS");
    return;
  }
  int idx = atoi(a1);
  if (idx < 1 || idx > count)
  {
    reply_err("RANGE");
    return;
  }
  HAL_GPIO_WritePin(table[idx - 1].port, table[idx - 1].pin, atoi(a2) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  reply_ok();
}

static void digital_get(const gpio_t *table, int count)
{
  char *a1 = strtok(NULL, " \t");
  if (!a1)
  {
    reply_err("ARGS");
    return;
  }
  int idx = atoi(a1);
  if (idx < 1 || idx > count)
  {
    reply_err("RANGE");
    return;
  }
  reply_val(HAL_GPIO_ReadPin(table[idx - 1].port, table[idx - 1].pin));
}

/* IN? ohne Nummer liefert alle 8 Eingaenge als Bitmaske, IN1 zuerst. */
static void cmd_in_query(void)
{
  char *a1 = strtok(NULL, " \t");
  if (!a1)
  {
    char bits[9];
    for (int i = 0; i < 8; i++)
    {
      bits[i] = HAL_GPIO_ReadPin(in_gpio[i].port, in_gpio[i].pin) ? '1' : '0';
    }
    bits[8] = '\0';
    char out[12];
    snprintf(out, sizeof(out), "%s\r\n", bits);
    send_line(out);
    return;
  }
  int idx = atoi(a1);
  if (idx < 1 || idx > 8)
  {
    reply_err("RANGE");
    return;
  }
  reply_val(HAL_GPIO_ReadPin(in_gpio[idx - 1].port, in_gpio[idx - 1].pin));
}

/* cal enthaelt (naiver DAC-Sollwert -> tatsaechliche Ausgangsspannung);
 * fuer die Ansteuerung brauchen wir die Umkehrfunktion, also x/y vertauscht. */
static int32_t cal_invert(const cal_point_t *cal, int32_t y)
{
  cal_point_t inv = {cal->y1, cal->x1, cal->y2, cal->x2};
  return Cal_Apply(&inv, y);
}

static void cmd_aout(void)
{
  char *a1 = strtok(NULL, " \t");
  char *a2 = strtok(NULL, " \t");
  if (!a1 || !a2)
  {
    reply_err("ARGS");
    return;
  }
  int idx = atoi(a1);
  if (idx < 1 || idx > 2)
  {
    reply_err("RANGE");
    return;
  }
  int32_t naive_mv = cal_invert(&cal_aout[idx - 1], atoi(a2));
  dac_write_mv(dac_channel[idx - 1], naive_mv);
  reply_ok();
}

/* Steuert den DAC direkt im unkalibrierten 0..3300-mV-Sollwertbereich an,
 * ohne die AOUT-Kalibrierung anzuwenden - fuer die Kalibrierprozedur
 * (docs/calibration.md) und zu Diagnosezwecken. */
static void cmd_aout_raw(void)
{
  char *a1 = strtok(NULL, " \t");
  char *a2 = strtok(NULL, " \t");
  if (!a1 || !a2)
  {
    reply_err("ARGS");
    return;
  }
  int idx = atoi(a1);
  if (idx < 1 || idx > 2)
  {
    reply_err("RANGE");
    return;
  }
  dac_write_mv(dac_channel[idx - 1], atoi(a2));
  reply_ok();
}

static void cmd_ain_query(void)
{
  char *a1 = strtok(NULL, " \t");
  if (!a1)
  {
    reply_err("ARGS");
    return;
  }
  int idx = atoi(a1);
  if (idx < 1 || idx > 4)
  {
    reply_err("RANGE");
    return;
  }
  int32_t naive_mv = adc_read_mv(ain_channel[idx - 1]);
  reply_val(naive_mv < 0 ? naive_mv : Cal_Apply(&cal_ain[idx - 1], naive_mv));
}

/* Liefert den unkalibrierten ("naiven", raw*3300/4095) mV-Wert ohne
 * Anwendung von cal_ain - fuer die Kalibrierprozedur (docs/calibration.md)
 * und zu Diagnosezwecken. */
static void cmd_ain_raw_query(void)
{
  char *a1 = strtok(NULL, " \t");
  if (!a1)
  {
    reply_err("ARGS");
    return;
  }
  int idx = atoi(a1);
  if (idx < 1 || idx > 4)
  {
    reply_err("RANGE");
    return;
  }
  reply_val(adc_read_mv(ain_channel[idx - 1]));
}

static void cmd_curr_query(void)
{
  char *a1 = strtok(NULL, " \t");
  if (!a1)
  {
    reply_err("ARGS");
    return;
  }
  int idx = atoi(a1);
  if (idx < 1 || idx > 2)
  {
    reply_err("RANGE");
    return;
  }
  int32_t naive_mv = adc_read_mv(curr_channel[idx - 1]);
  reply_val(naive_mv < 0 ? naive_mv : Cal_Apply(&cal_curr[idx - 1], naive_mv));
}

/* Liefert die rohe Sense-Spannung in mV ohne Anwendung von cal_curr - fuer
 * die Kalibrierprozedur (docs/calibration.md) und zu Diagnosezwecken. */
static void cmd_curr_raw_query(void)
{
  char *a1 = strtok(NULL, " \t");
  if (!a1)
  {
    reply_err("ARGS");
    return;
  }
  int idx = atoi(a1);
  if (idx < 1 || idx > 2)
  {
    reply_err("RANGE");
    return;
  }
  reply_val(adc_read_mv(curr_channel[idx - 1]));
}

/* Eindeutige Geraete-ID aus der 96-bit STM32-UID (UID_BASE), damit ein Host
 * (z.B. LabControl) ueber das Kommandoprotokoll dasselbe physische Board
 * wiedererkennen kann - z.B. um bekannte Hardware-Defekte je Exemplar
 * auszublenden (siehe docs/hardware-notes.md). Dieselbe Kombination der
 * UID-Woerter wie in USB_DEVICE/App/usbd_desc.c (Get_SerialNum), damit die
 * ID mit der USB-Seriennummer (unter Windows als "SER=..." sichtbar)
 * uebereinstimmt. */
static void cmd_idn_query(void)
{
  uint32_t uid0 = *(uint32_t *)UID_BASE;
  uint32_t uid1 = *(uint32_t *)(UID_BASE + 0x4);
  uint32_t uid2 = *(uint32_t *)(UID_BASE + 0x8);
  char buf[48];
  snprintf(buf, sizeof(buf), "microHIL,fw=0.1.0,SN=%08lX%04lX\r\n",
           (unsigned long)(uid0 + uid2), (unsigned long)(uid1 >> 16));
  send_line(buf);
}

static void cmd_pwr12_set(void)
{
  char *a1 = strtok(NULL, " \t");
  char *a2 = strtok(NULL, " \t");
  if (!a1 || !a2)
  {
    reply_err("ARGS");
    return;
  }
  int idx = atoi(a1);
  if (idx < 1 || idx > 2)
  {
    reply_err("RANGE");
    return;
  }
  int i = idx - 1;
  int on = atoi(a2) ? 1 : 0;

  pwr12_ch[i].requested_on = (uint8_t)on;
  if (!on)
  {
    /* Schaltanforderung zurueckgenommen: eigene Ueberstromverriegelung
     * dieses Kanals loesen; das Sammelbudget-Latch erst, wenn BEIDE Kanaele
     * zurueckgenommen wurden. */
    pwr12_ch[i].oc_latched = 0;
    pwr12_ch[i].over_active = 0;
    if (!pwr12_ch[0].requested_on && !pwr12_ch[1].requested_on)
    {
      pwr12_budget_latched = 0;
    }
  }
  pwr12_apply(i);
  reply_ok();
}

/* Diagnose, warum ein Kanal trotz requested_on=1 nicht schaltet (PWR12?
 * liefert dann weiterhin 0, reine GPIO-Rueckleseung): Bit0 = Kanal per
 * eigenem Ueberstromlimit verriegelt, Bit1 = gemeinsames Eingangsbudget
 * (PWR12_TOTAL_BUDGET_MA) verriegelt beide Kanaele. Beide loesen sich erst,
 * wenn die betroffene(n) Schaltanforderung(en) einmal per PWR12 <n> 0
 * zurueckgenommen wurden. */
static void cmd_pwr12_flt_query(void)
{
  char *a1 = strtok(NULL, " \t");
  if (!a1)
  {
    reply_err("ARGS");
    return;
  }
  int idx = atoi(a1);
  if (idx < 1 || idx > 2)
  {
    reply_err("RANGE");
    return;
  }
  int32_t flags = 0;
  if (pwr12_ch[idx - 1].oc_latched) flags |= 1;
  if (pwr12_budget_latched) flags |= 2;
  reply_val(flags);
}

static void cmd_ilim_set(void)
{
  char *a1 = strtok(NULL, " \t");
  char *a2 = strtok(NULL, " \t");
  if (!a1 || !a2)
  {
    reply_err("ARGS");
    return;
  }
  int idx = atoi(a1);
  if (idx < 1 || idx > 2)
  {
    reply_err("RANGE");
    return;
  }
  int32_t ma = atoi(a2);
  if (ma < 0)
  {
    ma = 0;
  }
  pwr12_ch[idx - 1].limit_ma = ma;
  reply_ok();
}

static void cmd_ilim_query(void)
{
  char *a1 = strtok(NULL, " \t");
  if (!a1)
  {
    reply_err("ARGS");
    return;
  }
  int idx = atoi(a1);
  if (idx < 1 || idx > 2)
  {
    reply_err("RANGE");
    return;
  }
  reply_val(pwr12_ch[idx - 1].limit_ma);
}

static void handle_line(char *line)
{
  char *cmd = strtok(line, " \t");
  if (!cmd)
  {
    return;
  }

  if (strcmp(cmd, "*IDN?") == 0)       { cmd_idn_query(); }
  else if (strcmp(cmd, "RELAY") == 0)  { digital_set(relay_gpio, 4); }
  else if (strcmp(cmd, "RELAY?") == 0) { digital_get(relay_gpio, 4); }
  else if (strcmp(cmd, "OUT") == 0)    { cmd_out_set(); }
  else if (strcmp(cmd, "OUT?") == 0)   { digital_get(out_gpio, 8); }
  else if (strcmp(cmd, "IN?") == 0)    { cmd_in_query(); }
  else if (strcmp(cmd, "AOUT") == 0)   { cmd_aout(); }
  else if (strcmp(cmd, "AOUTRAW") == 0){ cmd_aout_raw(); }
  else if (strcmp(cmd, "AIN?") == 0)   { cmd_ain_query(); }
  else if (strcmp(cmd, "AINRAW?") == 0){ cmd_ain_raw_query(); }
  else if (strcmp(cmd, "PWM") == 0)    { cmd_pwm(); }
  else if (strcmp(cmd, "PWM?") == 0)   { cmd_pwm_query(); }
  else if (strcmp(cmd, "PWR12") == 0)  { cmd_pwr12_set(); }
  else if (strcmp(cmd, "PWR12?") == 0) { digital_get(pwr12_gpio, 2); }
  else if (strcmp(cmd, "PWR12FLT?") == 0) { cmd_pwr12_flt_query(); }
  else if (strcmp(cmd, "ILIM") == 0)   { cmd_ilim_set(); }
  else if (strcmp(cmd, "ILIM?") == 0)  { cmd_ilim_query(); }
  else if (strcmp(cmd, "CURR?") == 0)  { cmd_curr_query(); }
  else if (strcmp(cmd, "CURRRAW?") == 0) { cmd_curr_raw_query(); }
  else                                  { reply_err("UNKNOWN"); }
}

void Protocol_Poll(void)
{
  pwr12_guard_poll();

  while (rx_tail != rx_head)
  {
    uint8_t byte = rx_ring[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1) % RX_RING_SIZE);

    if (byte == '\n')
    {
      if (line_len > 0 && line_buf[line_len - 1] == '\r')
      {
        line_len--;
      }
      line_buf[line_len] = '\0';
      handle_line(line_buf);
      line_len = 0;
    }
    else if (line_len < (LINE_MAX - 1))
    {
      line_buf[line_len++] = (char)byte;
    }
    else
    {
      /* Zeile zu lang: verwerfen und auf naechstes \n warten */
      line_len = 0;
    }
  }
}
