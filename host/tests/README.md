# Prüfskripte (ohne Hardware)

Beide Skripte laufen ohne angeschlossenen microHIL und sichern die zwei
Stellen ab, an denen sich Fehler still einschleichen: das CAN-Bit-Timing und
das SLCAN-Drahtformat.

```
python host/tests/check_bit_timing.py
python host/tests/check_slcan_wire.py
```

- **`check_bit_timing.py`** bildet `timing_for_bitrate()` aus
  `Core/Src/slcan.c` nach und prüft für alle `Sn`-Bitraten, dass die
  Nennbitrate exakt getroffen wird und BS1/BS2/Prescaler in den
  bxCAN-Registergrenzen liegen (BS1 ≤ 16 tq, BS2 ≤ 8 tq).
- **`check_slcan_wire.py`** prüft beide Richtungen des Protokolls gegen den
  echten `python-can`-Treiber: dass die Firmware alles versteht, was
  `python-can` sendet (inklusive der Kommandofolge beim Verbinden), und dass
  `python-can` alles dekodiert, was die Firmware erzeugt.

Wird die Firmware an diesen Stellen geändert, müssen die Nachbildungen in den
Skripten mitgezogen werden — sie sind bewusst eine unabhängige zweite
Implementierung, kein Import.
