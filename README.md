# microHIL

Eigenentwicklung: Testgerät mit schaltbaren Relais sowie digitalen und
analogen Ein-/Ausgängen, gesteuert von einem STM32F446RET (LQFP64).

## Struktur

- `firmware/` — STM32-Projekt (STM32Cube for VS Code), läuft auf dem F446RET
- `host/` — PC-seitige Ansteuerung/Auswertung (Python)
- `docs/` — Schaltpläne, Pinbelegung, Notizen

## Kommunikation

- Primär: USB (CDC / Virtual COM Port)
- Geplant (später): CAN

## Hardware

- MCU: STM32F446RET, LQFP64
- Debug: SWD, ST-Link V2
