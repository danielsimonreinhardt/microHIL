# Changelog

## Unreleased

- Projektgerüst angelegt (firmware/, host/, docs/)
- STM32CubeMX-Projekt `firmware/microHIL_fw` für STM32F446RET (CMake-Toolchain) generiert
  - USB OTG FS als Device mit CDC-Klasse (Virtual COM Port)
  - Pinbelegung: 4x Relais, 8x digitaler Eingang, 8x digitaler Ausgang, 4x Analogeingang, 2x Analogausgang (DAC), 2x schaltbarer 12V-Ausgang mit Strommessung, CAN1/CAN2 (Pins reserviert, Ansteuerung folgt später)
- USB-CDC-Echo (Virtual COM Port) funktionsfähig, End-to-End mit `host/test_echo.py` verifiziert
  - Fix: VBUS-Sensing deaktiviert (PA9 auf Platine nicht mehr verbunden, siehe `docs/hardware-notes.md`)
  - Fix: Clock-Tree in `SystemClock_Config()` korrigiert (HSE/PLL als Quelle statt fälschlich HSI) — verbauter Quarz läuft mit 8 statt 16 MHz, PLLM entsprechend angepasst
- Kommandoprotokoll für Relais/DIO/AIO/AOUT/12V-Ausgänge implementiert (`Core/Src/protocol.c`), Referenz in `docs/protocol.md`, Python-Client `host/microhil.py` — End-to-End auf Hardware getestet
- Test-GUI (`host/gui.py`, PySide6) zum manuellen Durchtesten aller Funktionen
- Fix: AOUT1/AOUT2-Zuordnung war vertauscht (PA4/PA5-Labels in CubeMX korrigiert, `dac_channel[]` in `protocol.c` entsprechend angepasst)
- PWM-Kanäle PWM1-4 (PC6-9, TIM3) im Protokoll ergänzt, softwareseitig gegen OUT1-4 verriegelt (dieselbe Endstufe laut Schaltplan) — **noch nicht auf Hardware getestet**, ST-Link war beim Umsetzen getrennt. Build erfolgreich, GUI-Konstruktion geprüft.
- CAN1 als CAN-USB-Interface im SLCAN-/Lawicel-Format (`Core/Src/can_if.c`, `Core/Src/slcan.c`), nutzbar mit `python-can` (`interface="slcan"`) und unter Linux via `slcand` als SocketCAN-Interface — **noch nicht auf Hardware getestet**
  - bxCAN-Treiber mit Interrupt-RX (64-Frame-Ring), Accept-all-Filter, automatischer Bus-Off-Erholung und SLCAN-Fehlerflags; Bit-Timing wird zur Laufzeit gesucht statt in CubeMX hinterlegt, damit ein Regenerieren des `.ioc` die CAN-Anbindung nicht zerlegt
  - Filterbank-Split gesetzt (`SlaveStartFilterBank = 14`), damit CAN2 später überhaupt empfangen kann
  - Zusatzkommandos über CAN232 hinaus: `B<bit/s>` für krumme Bitraten (u. a. 750 kbit/s, das `python-can` fälschlich auf `S7` mappt) und `Y` für Loopback-Selbsttest ohne Bus
