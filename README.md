# microHIL

Eigenentwicklung: Testgerät mit schaltbaren Relais sowie digitalen und
analogen Ein-/Ausgängen, gesteuert von einem STM32F446RET (LQFP64).

## Struktur

- `firmware/` — STM32-Projekt (STM32Cube for VS Code), läuft auf dem F446RET
- `host/` — PC-seitige Ansteuerung/Auswertung (Python)
- `docs/` — Schaltpläne, Pinbelegung, Notizen

## Kommunikation

microHIL meldet sich als USB-Composite-Device mit zwei virtuellen COM-Ports:

- **Port 1** (Interface 0) — Kommandoprotokoll für Relais, DIO, AIO, PWM und
  die 12V-Ausgänge, siehe [`docs/protocol.md`](docs/protocol.md)
- **Port 2** (Interface 2) — CAN1 als CAN-USB-Interface im SLCAN-Format,
  siehe [`docs/can-usb.md`](docs/can-usb.md). Nutzbar mit `python-can` und
  unter Linux via `slcand` als SocketCAN-Interface.

Beide Ports lassen sich gleichzeitig benutzen.

## Hardware

- MCU: STM32F446RET, LQFP64
- Debug: SWD, ST-Link V2
- CAN1: PB8/PB9, Transceiver und 120-Ω-Abschluss bestückt
- CAN2: PB5/PB6, noch nicht in Betrieb (siehe Roadmap)

## Roadmap

- **CAN2 als Fernsteuer-Schnittstelle.** Der microHIL soll über CAN2 von einem
  übergeordneten Steuergerät oder Prüfstand bedient werden können — dieselben
  Funktionen wie über das USB-Kommandoprotokoll (Relais, DIO, AIO, PWM,
  12V-Ausgänge). Frame-Layout, Adressierung und Bitrate sind noch zu
  definieren. Vorbereitet ist bereits die Aufteilung der bxCAN-Filterbänke
  (`SlaveStartFilterBank = 14` in `Core/Src/can_if.c`), ohne die CAN2 gar
  nicht empfangen könnte.
- **`CURR?` in mA umrechnen.** Liefert aktuell die rohe Sense-Spannung in mV;
  der Shunt-/Verstärkungsfaktor der Strommess-Schaltung fehlt noch, siehe
  [`docs/protocol.md`](docs/protocol.md).
- **PWM1-4 auf Hardware verifizieren.** Implementiert und verriegelt, aber
  noch nicht am Gerät getestet.
