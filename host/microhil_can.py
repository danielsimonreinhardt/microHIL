"""CAN1 des microHIL als CAN-USB-Interface (SLCAN) ansprechen.

Duenner Wrapper um python-can: findet den richtigen der beiden COM-Ports und
umgeht die S7-Mehrdeutigkeit der SLCAN-Bitratentabelle.

    from microhil_can import open_can

    with open_can(500000) as bus:
        bus.send(can.Message(arbitration_id=0x7DF, data=[0x02, 0x01, 0x00]))
        print(bus.recv(1.0))

Als Sniffer direkt aufrufbar:

    python microhil_can.py 500000
"""
import sys

import can
import serial

from microhil import find_can_port

# python-can bildet 750000 auf 'S7' ab, das originale Lawicel-CAN232 (und
# damit die microHIL-Firmware) versteht unter 'S7' aber 800 kbit/s. Fuer diese
# beiden Raten -- und fuer alles, was gar nicht in der Tabelle steht -- setzen
# wir die Bitrate vorab selbst per 'B<bit/s>'-Zusatzkommando und lassen
# python-can den Bus nur noch oeffnen. Siehe docs/can-usb.md.
_SLCAN_SAFE_BITRATES = {10000, 20000, 50000, 100000, 125000,
                        250000, 500000, 1000000}


def set_bitrate_raw(port: str, bitrate: int, timeout: float = 1.0) -> None:
    """Bitrate ueber das B-Kommando setzen, ohne python-cans Tabelle.

    Der Bus wird dabei geschlossen -- das Setzen der Bitrate ist bei
    geschlossenem Bus sowieso Pflicht.
    """
    with serial.Serial(port, baudrate=115200, timeout=timeout) as ser:
        ser.write(b"C\r")
        ser.read(1)
        ser.reset_input_buffer()
        ser.write(f"B{bitrate}\r".encode("ascii"))
        reply = ser.read(1)
        if reply != b"\r":
            raise RuntimeError(
                f"microHIL hat die Bitrate {bitrate} abgelehnt "
                f"(Antwort {reply!r}). Zulaessig sind 5000..1000000 bit/s, "
                "sofern bei 36 MHz auf 0,1 % genau erreichbar."
            )


def open_can(bitrate: int = 500000, port: str | None = None,
             listen_only: bool = False, **kwargs) -> can.BusABC:
    """CAN1 oeffnen und einen python-can-Bus zurueckgeben."""
    port = port or find_can_port()

    if bitrate in _SLCAN_SAFE_BITRATES:
        return can.Bus(interface="slcan", channel=port, bitrate=bitrate,
                       listen_only=listen_only, **kwargs)

    set_bitrate_raw(port, bitrate)
    return can.Bus(interface="slcan", channel=port,
                   listen_only=listen_only, **kwargs)


def _main(argv: list[str]) -> int:
    bitrate = int(argv[1]) if len(argv) > 1 else 500000
    port = find_can_port()
    print(f"microHIL CAN1 auf {port}, {bitrate} bit/s -- Abbruch mit Strg-C")

    with open_can(bitrate, port=port) as bus:
        try:
            for msg in bus:
                print(msg)
        except KeyboardInterrupt:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(_main(sys.argv))
