# CAN1 als CAN-USB-Interface (SLCAN)

CAN1 des microHIL ist ein CAN-USB-Adapter im SLCAN-/Lawicel-Format
(CAN232-kompatibel). Damit funktioniert es ohne eigenen Treiber mit
`python-can` unter Windows und unter Linux über `slcand` als reguläres
SocketCAN-Interface.

CAN2 ist für die spätere Fernsteuerung des microHIL reserviert und hier
nicht angesprochen (siehe Roadmap im README).

## Zwei COM-Ports

microHIL meldet sich als USB-Composite-Device mit **zwei** CDC-ACM-Funktionen,
also zwei virtuellen COM-Ports:

| Interface | Windows | Linux | Inhalt |
|---|---|---|---|
| 0 | `...&MI_00` | `...-if00` | HIL-Kommandoprotokoll (`docs/protocol.md`) |
| 2 | `...&MI_02` | `...-if02` | CAN1, SLCAN |

Beide Ports haben dieselbe VID:PID `0483:5740`. Der Composite-Builder der
ST-USB-Library vergibt für beide CDC-Funktionen den Interface-String-Index 0,
deshalb heißen im Windows-Gerätemanager beide Ports gleich — auseinanderhalten
lassen sie sich nur über die Interface-Nummer. `host/microhil.py` macht das
automatisch (`find_ports()`, `find_can_port()`).

Der Sinn der Trennung: ein COM-Port lässt sich unter Windows nur einmal öffnen.
Mit zwei Ports laufen Test-GUI und CAN-Werkzeug gleichzeitig.

## Kommandos

Kommandos sind mit **CR** (`\r`) terminiert, nicht mit `\n` wie beim
HIL-Protokoll. Antwort ist `\r` (OK) bzw. `\a` (BEL, Fehler).

| Befehl | Bedeutung | Antwort |
|---|---|---|
| `Sn` | Bitrate wählen, `n` = 0-9 (Tabelle unten) | `\r` |
| `B<bit/s>` | **microHIL-Erweiterung:** exakte Bitrate dezimal, z. B. `B750000` | `\r` |
| `O` | Bus öffnen, Normalbetrieb | `\r` |
| `L` | Bus öffnen, Listen-Only (sendet nichts, auch kein ACK) | `\r` |
| `Y` | **microHIL-Erweiterung:** Loopback-Selbsttest ohne Bus | `\r` |
| `C` | Bus schließen | `\r` |
| `tiiildd…` | Standard-Frame (11 bit) senden | `z\r` |
| `Tiiiiiiiildd…` | Extended-Frame (29 bit) senden | `Z\r` |
| `riiil` | Standard-RTR senden | `z\r` |
| `Riiiiiiiil` | Extended-RTR senden | `Z\r` |
| `F` | Statusflags lesen (das Lesen löscht sie) | `Fxx\r` |
| `V` | Version | `V0100\r` |
| `N` | Seriennummer (4 Hex aus der MCU-UID) | `Nxxxx\r` |
| `Zn` | Zeitstempel aus (`Z0`) / ein (`Z1`) | `\r` |

Empfangene Frames kommen unaufgefordert im selben `t`/`T`/`r`/`R`-Format.
Bei `Z1` hängen 4 Hex-Ziffern Millisekunden an (Überlauf bei 60000 = `EA5F`).

Die Bitrate lässt sich nur bei **geschlossenem** Bus setzen. `O`/`L`/`Y` sind
idempotent (schließen intern vorher) — `python-can` schickt beim Verbinden
zweimal `O`.

### Statusflags (`F`)

| Bit | Bedeutung |
|---|---|
| 0 | RX-Ringpuffer voll (Frames verworfen) |
| 1 | TX-Mailboxen voll (Frame verworfen) |
| 2 | Error Warning |
| 3 | Data Overrun |
| 5 | Error Passive |
| 6 | Arbitration Lost |
| 7 | Bus Error |

## Bitraten

`Sn` folgt dem originalen Lawicel-CAN232.

| Befehl | Bitrate | Prescaler | BS1 | BS2 | SJW | tq/Bit | Sample Point |
|---|---|---|---|---|---|---|---|
| `S0` | 10 kbit/s | 180 | 16 | 3 | 1 | 20 | 85,0 % |
| `S1` | 20 kbit/s | 90 | 16 | 3 | 1 | 20 | 85,0 % |
| `S2` | 50 kbit/s | 36 | 16 | 3 | 1 | 20 | 85,0 % |
| `S3` | 100 kbit/s | 18 | 16 | 3 | 1 | 20 | 85,0 % |
| `S4` | 125 kbit/s | 16 | 15 | 2 | 1 | 18 | 88,9 % |
| `S5` | 250 kbit/s | 8 | 15 | 2 | 1 | 18 | 88,9 % |
| `S6` | 500 kbit/s | 4 | 15 | 2 | 1 | 18 | 88,9 % |
| `S7` | **800 kbit/s** | 3 | 10 | 4 | 1 | 15 | 73,3 % |
| `S8` | 1 Mbit/s | 2 | 13 | 4 | 1 | 18 | 77,8 % |
| `S9` | 83,333 kbit/s | 24 | 15 | 2 | 1 | 18 | 88,9 % |

Alle Nennbitraten werden bei PCLK1 = 36 MHz exakt getroffen. Die Werte stehen
nicht als Tabelle in der Firmware, sondern werden von `timing_for_bitrate()`
in `Core/Src/slcan.c` gesucht: feinste Auflösung, deren Sample Point nahe genug
am Ziel liegt (87,5 %, ab 800 kbit/s 75 %), bei höchstens 0,1 % Bitratenfehler.
Grenzen der bxCAN-Register: BS1 ≤ 16 tq, BS2 ≤ 8 tq, Prescaler ≤ 1024.

### Stolperstein: `S7`

`python-can` bildet **750 kbit/s** auf `S7` ab, das originale Lawicel-CAN232
(und damit microHIL) versteht unter `S7` aber **800 kbit/s**. Wer 750 kbit/s
braucht, setzt sie über das `B`-Kommando:

```
C
B750000
O
```

`host/microhil_can.py` macht genau das automatisch für alle Bitraten außerhalb
der eindeutigen Menge.

## Benutzung

### Windows / Python

```python
from microhil_can import open_can
import can

with open_can(500000) as bus:
    bus.send(can.Message(arbitration_id=0x7DF, is_extended_id=False,
                         data=[0x02, 0x01, 0x00]))
    print(bus.recv(1.0))
```

Als Sniffer:

```
python host/microhil_can.py 500000
```

Oder direkt mit python-can, wenn der Port bekannt ist:

```python
bus = can.Bus(interface="slcan", channel="COM8", bitrate=500000)
```

In der Test-GUI (`host/gui.py`) gibt es dafür den Reiter **CAN1** mit
Bitratenwahl, Listen-Only, Trace und Sendefeld. HIL-Reiter und CAN-Reiter
benutzen die beiden getrennten COM-Ports und funktionieren gleichzeitig.

### Linux / Raspberry Pi (SocketCAN)

```
slcand -o -s6 -t hw -S 115200 /dev/ttyACM1 can0
ip link set can0 up
candump can0
```

`-s6` = 500 kbit/s (siehe Tabelle). `/dev/ttyACM1` ist der Port mit
Interface 2 — verlässlich über `/dev/serial/by-id/*-if02` ansprechen, weil
sich die `ttyACM`-Nummern beim Anstecken verschieben können.

Damit steht `can0` allen SocketCAN-Werkzeugen zur Verfügung, unter anderem
`can-utils` und `uds-diag` — dort ist keine Anpassung nötig.

## Hardware

- CAN1: PB8 = RX, PB9 = TX, Transceiver bestückt, 120 Ω auf der Platine.
- `CAN1_SILENT` (PB7) wird auf LOW gehalten (Normalbetrieb). Listen-Only wird
  über den Silent-Modus des bxCAN abgebildet, nicht über den Transceiver.

## Implementierung

- `Core/Src/can_if.c` — bxCAN-Treiber: Bit-Timing, Accept-all-Filter,
  Interrupt-RX in einen 64-Frame-Ring, Fehlerflags, automatische
  Bus-Off-Erholung. Die Filterbänke sind aufgeteilt
  (`SlaveStartFilterBank = 14`), damit CAN2 später empfangen kann.
- `Core/Src/slcan.c` — Protokoll-Parser und -Serializer, gepufferte Ausgabe.
- `USB_DEVICE/App/usbd_cdc_if.c` — zweite CDC-Instanz (`..._fops_CAN`).
- `host/microhil_can.py`, `host/microhil.py` (`find_can_port()`).

### Hinweis zu CubeMX

Das Composite-Device ist von Hand eingerichtet, CubeMX generiert es für die
F4-Serie nicht. Nach einem Regenerieren aus der `.ioc` sind zu prüfen:

- `USB_DEVICE/Target/usbd_conf.h`: `USBD_MAX_NUM_INTERFACES` muss `4U` sein
  (der Composite-Block selbst steht im `USER CODE`-Bereich und überlebt).
- `USB_DEVICE/Target/usbd_conf.c`: die FIFO-Aufteilung (fünf TX-FIFOs).
- `USB_DEVICE/App/usb_device.c`: die beiden `USBD_RegisterClassComposite()`.
- `USB_DEVICE/App/usbd_desc.c`: die Gerätestrings.

Das CAN-Bit-Timing ist bewusst **nicht** von CubeMX abhängig: `can_if.c`
konfiguriert `hcan1` vollständig selbst, die Werte in `MX_CAN1_Init()` sind
irrelevant. Ebenso liegen die CAN-IRQ-Handler in einem `USER CODE`-Block von
`stm32f4xx_it.c` statt im NVIC-Tab.
