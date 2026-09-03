"""Prueft das SLCAN-Drahtformat der Firmware gegen den echten python-can-Treiber.

Die Firmware selbst laesst sich hier nicht ausfuehren (kein Host-Compiler), aber
beide Richtungen des Formats lassen sich gegen python-can verifizieren:

  1. Was python-can sendet, muss die Grammatik von parse_and_send() in slcan.c
     erfuellen.
  2. Was emit_frame() in slcan.c erzeugt, muss python-can korrekt dekodieren.

emit_frame() ist dafuer unten 1:1 in Python nachgebildet.
"""
import re
import sys
from unittest import mock

import can
import serial

HEX = "0123456789ABCDEF"


# --- Nachbildung von emit_frame() aus Core/Src/slcan.c ----------------------

def emit_frame(can_id, dlc, ext, rtr, data, ts_ms=None):
    out = ("R" if ext else "r") if rtr else ("T" if ext else "t")
    out += f"{can_id:08X}" if ext else f"{can_id:03X}"
    out += HEX[dlc & 0xF]
    if not rtr:
        out += "".join(f"{b:02X}" for b in data[:dlc])
    if ts_ms is not None:
        out += f"{ts_ms:04X}"
    return out + "\r"


# --- Nachbildung der Kommando-Grammatik von handle_cmd()/parse_and_send() ---

FRAME_RE = re.compile(r"^([tTrR])([0-9A-Fa-f]+)$")
SIMPLE = {"O", "L", "C", "F", "V", "N", "Y"}


def firmware_accepts(cmd: str) -> tuple[bool, str]:
    """True, wenn slcan.c das Kommando annimmt (statt BEL zu antworten)."""
    if cmd == "":
        return True, "leer -> keine Antwort"
    c = cmd[0]
    if c == "S":
        return (len(cmd) == 2 and cmd[1].isdigit()), "Sn"
    if c == "B":
        return (len(cmd) >= 2 and cmd[1:].isdigit()), "B<bit/s>"
    if c == "Z":
        return (len(cmd) == 2 and cmd[1] in "01"), "Zn"
    if c in SIMPLE:
        return len(cmd) == 1, c
    m = FRAME_RE.match(cmd)
    if not m:
        return False, "unbekannt"
    kind, rest = m.groups()
    ext = kind in "TR"
    rtr = kind in "rR"
    id_digits = 8 if ext else 3
    if len(rest) < id_digits + 1:
        return False, "zu kurz"
    dlc = int(rest[id_digits], 16)
    if dlc > 8:
        return False, "DLC > 8"
    need = id_digits + 1 + (0 if rtr else dlc * 2)
    return len(rest) == need, f"{kind}-Frame"


# --- Fake-Serial, damit python-can ohne Hardware laeuft ---------------------

class FakeSerial:
    def __init__(self, *a, **kw):
        self.written = bytearray()
        self.to_read = bytearray()
        self.is_open = True
        self.write_timeout = None
        self.timeout = 0.001

    def write(self, data):
        self.written.extend(data)
        return len(data)

    def read(self, size=1):
        out = self.to_read[:size]
        del self.to_read[:size]
        return bytes(out)

    @property
    def in_waiting(self):
        return len(self.to_read)

    def flush(self):
        pass

    def reset_input_buffer(self):
        self.to_read.clear()

    def close(self):
        self.is_open = False


def make_bus(bitrate=None):
    fake = FakeSerial()
    with mock.patch.object(serial, "serial_for_url", return_value=fake):
        bus = can.Bus(interface="slcan", channel="fake", bitrate=bitrate,
                      sleep_after_open=0)
    return bus, fake


fails = []

# --- Richtung 1: python-can -> Firmware ------------------------------------

print("== Was python-can sendet, muss die Firmware verstehen ==")
bus, fake = make_bus(bitrate=500000)
fake.written.clear()

TX_CASES = [
    can.Message(arbitration_id=0x7DF, is_extended_id=False, data=b"\x02\x01\x00"),
    can.Message(arbitration_id=0x000, is_extended_id=False, data=b""),
    can.Message(arbitration_id=0x7FF, is_extended_id=False, data=bytes(range(8))),
    can.Message(arbitration_id=0x18DAF110, is_extended_id=True, data=b"\x10\x03"),
    can.Message(arbitration_id=0x1FFFFFFF, is_extended_id=True, data=bytes(8)),
    can.Message(arbitration_id=0x123, is_extended_id=False, is_remote_frame=True, dlc=4),
    can.Message(arbitration_id=0x1ABCDEF, is_extended_id=True, is_remote_frame=True, dlc=0),
]
for msg in TX_CASES:
    fake.written.clear()
    bus.send(msg)
    cmd = fake.written.decode().rstrip("\r")
    ok, why = firmware_accepts(cmd)
    print(f"  {'OK ' if ok else 'FEHLER'} {cmd:<32} ({why})")
    if not ok:
        fails.append(f"Firmware lehnt python-can-Kommando {cmd!r} ab")

# Verbindungsaufbau: welche Kommandos schickt python-can beim Oeffnen?
bus2, fake2 = make_bus(bitrate=500000)
seq = [c for c in fake2.written.decode().split("\r")]
print(f"\n  Sequenz beim Verbinden: {seq}")
for cmd in seq:
    ok, why = firmware_accepts(cmd)
    if not ok:
        fails.append(f"Firmware lehnt Verbindungskommando {cmd!r} ab")
        print(f"  FEHLER {cmd!r} ({why})")

# --- Richtung 2: Firmware -> python-can ------------------------------------

print("\n== Was die Firmware sendet, muss python-can dekodieren ==")
RX_CASES = [
    # (id, dlc, ext, rtr, data, ts)
    (0x7DF, 3, 0, 0, b"\x02\x01\x00", None),
    (0x7DF, 3, 0, 0, b"\x02\x01\x00", 0x1234),
    (0x000, 0, 0, 0, b"", None),
    (0x7FF, 8, 0, 0, bytes(range(8)), 0xEA5F),
    (0x18DAF110, 2, 1, 0, b"\x10\x03", None),
    (0x1FFFFFFF, 8, 1, 0, bytes(8), 0x0001),
    (0x123, 4, 0, 1, b"", None),
    (0x1ABCDEF, 0, 1, 1, b"", None),
]
for can_id, dlc, ext, rtr, data, ts in RX_CASES:
    line = emit_frame(can_id, dlc, ext, rtr, data, ts)
    bus3, fake3 = make_bus()
    fake3.to_read.extend(line.encode())
    msg = bus3.recv(0.5)
    if msg is None:
        print(f"  FEHLER {line!r:<34} -> nicht dekodiert")
        fails.append(f"python-can dekodiert {line!r} nicht")
        continue
    good = (msg.arbitration_id == can_id and msg.dlc == dlc
            and bool(msg.is_extended_id) == bool(ext)
            and bool(msg.is_remote_frame) == bool(rtr)
            and (rtr or bytes(msg.data) == data[:dlc]))
    print(f"  {'OK ' if good else 'FEHLER'} {line!r:<34} -> id={msg.arbitration_id:X} "
          f"dlc={msg.dlc} ext={int(msg.is_extended_id)} rtr={int(msg.is_remote_frame)} "
          f"data={bytes(msg.data).hex() or '-'}")
    if not good:
        fails.append(f"Fehldekodierung von {line!r}")

# --- Antworten auf Kommandos ------------------------------------------------

print("\n== Firmware-Antworten stoeren den Empfang nicht ==")
for label, reply in [("OK (CR)", "\r"), ("Fehler (BEL)", "\a"),
                     ("TX-Ack std", "z\r"), ("TX-Ack ext", "Z\r"),
                     ("Status", "F00\r"), ("Version", "V0100\r"),
                     ("Serie", "N1A2B\r")]:
    bus4, fake4 = make_bus()
    fake4.to_read.extend((reply + emit_frame(0x100, 1, 0, 0, b"\xAA")).encode())
    msg = bus4.recv(0.5)
    good = msg is not None and msg.arbitration_id == 0x100
    print(f"  {'OK ' if good else 'FEHLER'} {label:<14} {reply!r:<10} -> "
          f"{'Frame danach erkannt' if good else 'Frame verschluckt'}")
    if not good:
        fails.append(f"Antwort {reply!r} verschluckt den folgenden Frame")

print()
if fails:
    print("FEHLGESCHLAGEN:")
    for f in fails:
        print("  -", f)
    sys.exit(1)
print("alle Pruefungen bestanden")
