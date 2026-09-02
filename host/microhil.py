"""Python-Client fuer das microHIL-Kommandoprotokoll (USB-CDC)."""
import sys

import serial
import serial.tools.list_ports

VID = 0x0483
PID = 0x5740


class MicroHILError(RuntimeError):
    pass


def find_port() -> str:
    for port in serial.tools.list_ports.comports():
        if port.vid == VID and port.pid == PID:
            return port.device
    sys.exit("microHIL nicht gefunden (VID:PID 0483:5740). port= manuell angeben.")


class MicroHIL:
    def __init__(self, port: str | None = None, timeout: float = 2.0):
        self._ser = serial.Serial(port or find_port(), baudrate=115200, timeout=timeout)

    def close(self) -> None:
        self._ser.close()

    def __enter__(self) -> "MicroHIL":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def _command(self, line: str) -> str:
        self._ser.write((line + "\n").encode("ascii"))
        reply = self._ser.readline().decode("ascii", errors="replace").strip()
        if not reply:
            raise MicroHILError(f"keine Antwort auf {line!r}")
        if reply.startswith("ERR"):
            raise MicroHILError(f"{line!r} -> {reply}")
        return reply

    def idn(self) -> str:
        return self._command("*IDN?")

    def set_relay(self, n: int, state: bool) -> None:
        self._command(f"RELAY {n} {int(state)}")

    def get_relay(self, n: int) -> bool:
        return self._command(f"RELAY? {n}") == "1"

    def set_out(self, n: int, state: bool) -> None:
        self._command(f"OUT {n} {int(state)}")

    def get_out(self, n: int) -> bool:
        return self._command(f"OUT? {n}") == "1"

    def get_in(self, n: int) -> bool:
        return self._command(f"IN? {n}") == "1"

    def get_in_all(self) -> list[bool]:
        bits = self._command("IN?")
        return [c == "1" for c in bits]

    def set_aout_mv(self, n: int, millivolt: int) -> None:
        self._command(f"AOUT {n} {millivolt}")

    def get_ain_mv(self, n: int) -> int:
        return int(self._command(f"AIN? {n}"))

    def set_pwr12(self, n: int, state: bool) -> None:
        self._command(f"PWR12 {n} {int(state)}")

    def get_pwr12(self, n: int) -> bool:
        return self._command(f"PWR12? {n}") == "1"

    def get_curr_mv(self, n: int) -> int:
        """Rohe Sense-Spannung in mV (noch keine mA-Umrechnung, siehe docs/protocol.md)."""
        return int(self._command(f"CURR? {n}"))
