"""Sendet Testdaten an microHIL und prueft, ob sie unveraendert zurueckkommen."""
import argparse
import sys

import serial
import serial.tools.list_ports

VID = 0x0483
PID = 0x5740


def find_port() -> str:
    for port in serial.tools.list_ports.comports():
        if port.vid == VID and port.pid == PID:
            return port.device
    sys.exit("microHIL nicht gefunden (VID:PID 0483:5740). --port manuell angeben.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="z.B. COM5 (Default: automatische Erkennung)")
    parser.add_argument("--message", default="Hallo microHIL", help="Testnachricht")
    args = parser.parse_args()

    port = args.port or find_port()
    payload = args.message.encode("utf-8")

    with serial.Serial(port, baudrate=115200, timeout=2) as ser:
        ser.write(payload)
        echoed = ser.read(len(payload))

    if echoed == payload:
        print(f"OK ({port}): {echoed!r}")
    else:
        sys.exit(f"FEHLER ({port}): gesendet={payload!r} empfangen={echoed!r}")


if __name__ == "__main__":
    main()
