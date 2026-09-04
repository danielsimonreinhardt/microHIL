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
- CAN1 als CAN-USB-Interface im SLCAN-/Lawicel-Format (`Core/Src/can_if.c`, `Core/Src/slcan.c`), nutzbar mit `python-can` (`interface="slcan"`) und unter Linux via `slcand` als SocketCAN-Interface
  - bxCAN-Treiber mit Interrupt-RX (64-Frame-Ring), Accept-all-Filter, automatischer Bus-Off-Erholung und SLCAN-Fehlerflags; Bit-Timing wird zur Laufzeit gesucht statt in CubeMX hinterlegt, damit ein Regenerieren des `.ioc` die CAN-Anbindung nicht zerlegt
  - Filterbank-Split gesetzt (`SlaveStartFilterBank = 14`), damit CAN2 später überhaupt empfangen kann
  - Zusatzkommandos über CAN232 hinaus: `B<bit/s>` für krumme Bitraten (u. a. 750 kbit/s, das `python-can` fälschlich auf `S7` mappt) und `Y` für Loopback-Selbsttest ohne Bus
  - Referenz in `docs/can-usb.md`
  - Protokollschicht (Kommandoparser, Statusflags, Antworten) auf Hardware verifiziert; `O`/`L`/`Y` (Bus tatsächlich aufschalten) schlägt aktuell fehl, siehe `docs/hardware-notes.md` — **CAN1_RX (PB8) liegt offen, kein Transceiver treibt die Leitung**
- USB-Composite-Device mit zwei CDC-ACM-Funktionen (zwei virtuelle COM-Ports): Port 1 = HIL-Kommandoprotokoll, Port 2 = CAN1 — beide gleichzeitig nutzbar, weil sich ein COM-Port unter Windows nur einmal öffnen lässt — **auf Hardware verifiziert** (Enumeration, Parallelbetrieb beider Ports, GUI)
  - CompositeBuilder der ST-USB-Library aus dem passenden Cube-FW-Paket (F4 V1.28.3) ergänzt, CubeMX generiert das für die F4-Serie nicht selbst
  - FIFO-Aufteilung von OTG_FS neu vergeben (fünf TX-FIFOs statt zwei), Endpunktadressen fest zugeordnet
  - Gerätestrings auf `microHIL` geändert (vorher `STM32 Virtual ComPort`)
  - Fix: USB-Gerätedeskriptor deklarierte weiterhin `bDeviceClass=0x02` (CDC direkt am Gerät) statt der für IAD-Composite-Geräte nötigen Multi-Interface-Function-Klasse `0xEF/0x02/0x01` — Windows band ohne diesen Fix seinen CDC-Treiber direkt an Interface 0 und ignorierte den zweiten CDC-Funktionsblock komplett, sodass nur ein COM-Port erschien. Erst durch Hardware-Test gefunden.
- Host: `host/microhil_can.py` (python-can-Wrapper), `find_ports()`/`find_can_port()` in `host/microhil.py` zum Auseinanderhalten der beiden Ports, CAN-Reiter in `host/gui.py` mit Trace und Sendefeld, `python-can` in den Requirements
- Roadmap im README ergänzt, u. a. CAN2 als geplante Fernsteuer-Schnittstelle für microHIL
- Prüfskripte ohne Hardware in `host/tests/`: Bit-Timing gegen die bxCAN-Registergrenzen, SLCAN-Drahtformat in beiden Richtungen gegen den echten `python-can`-Treiber
- `host/test_echo.py` entfernt (Altlast aus der Bring-up-Phase, erwartete ein rohes Echo, das die Firmware seit dem Kommandoprotokoll nicht mehr liefert)
