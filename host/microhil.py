"""Python-Client fuer das microHIL-Kommandoprotokoll (USB-CDC)."""
import re
import sys

import serial
import serial.tools.list_ports

VID = 0x0483
PID = 0x5740

# microHIL meldet sich als Composite-Device mit zwei CDC-ACM-Funktionen. Beide
# COM-Ports haben dieselbe VID/PID, unterscheidbar sind sie nur ueber die
# Interface-Nummer: 0 = HIL-Kommandoprotokoll, 2 = CAN1 (SLCAN).
# Der Composite-Builder der ST-Library vergibt keine Interface-Namen, deshalb
# geht das nur ueber die hwid. Siehe docs/can-usb.md.
IFACE_CTRL = 0
IFACE_CAN = 2


class MicroHILError(RuntimeError):
    pass


def _interface_number(port) -> int | None:
    """Interface-Nummer eines Composite-Ports, plattformuebergreifend.

    pyserial legt die Nummer je nach Plattform und Version an verschiedenen
    Stellen ab, deshalb werden mehrere Schreibweisen probiert:
      * `location` bzw. `LOCATION=` in der hwid: `1-3:1.2` -> 2
      * Windows-Geraetepfad: `...&MI_02\\...`
      * Linux by-id-Pfad: `...-if02`
    """
    hwid = getattr(port, "hwid", "") or ""

    m = re.search(r"MI_([0-9A-Fa-f]+)", hwid)
    if m:
        return int(m.group(1), 16)

    for cand in (getattr(port, "device", "") or "", hwid):
        m = re.search(r"-if([0-9A-Fa-f]+)", cand)
        if m:
            return int(m.group(1), 16)

    location = getattr(port, "location", None) or ""
    if not location:
        m = re.search(r"LOCATION=(\S+)", hwid)
        if m:
            location = m.group(1)
    m = re.search(r"[:.](\d+)$", location)
    if m:
        return int(m.group(1))

    return None


def _natural_key(device: str):
    """COM9 vor COM10 sortieren (rein lexikalisch waere es umgekehrt)."""
    return [int(part) if part.isdigit() else part
            for part in re.split(r"(\d+)", device)]


def find_ports() -> dict[int, str]:
    """Alle microHIL-Ports als {Interface-Nummer: Geraetename}.

    Bei der alten Ein-Port-Firmware gibt es genau einen Port ohne
    Interface-Nummer; der wird als Steuerport gewertet.
    """
    matches = [p for p in serial.tools.list_ports.comports()
               if p.vid == VID and p.pid == PID]

    found: dict[int, str] = {}
    unknown = []
    for port in matches:
        iface = _interface_number(port)
        if iface is None:
            unknown.append(port.device)
        else:
            found[iface] = port.device

    if unknown:
        if len(matches) == 1:
            found[IFACE_CTRL] = unknown[0]
        else:
            # Letzter Ausweg, wenn die Plattform keine Interface-Nummer
            # herausrueckt: die Ports enumerieren in Interface-Reihenfolge,
            # der niedrigere ist also der Steuerport.
            for slot, device in zip((IFACE_CTRL, IFACE_CAN),
                                    sorted(unknown, key=_natural_key)):
                found.setdefault(slot, device)

    return found


def find_port(interface: int = IFACE_CTRL) -> str:
    ports = find_ports()
    if interface in ports:
        return ports[interface]
    if not ports:
        sys.exit("microHIL nicht gefunden (VID:PID 0483:5740). port= manuell angeben.")
    sys.exit(
        f"microHIL-Interface {interface} nicht gefunden. Gefunden: {ports}. "
        "Firmware mit Composite-USB geflasht? port= manuell angeben."
    )


def find_can_port() -> str:
    """COM-Port des CAN1-Interfaces (SLCAN)."""
    return find_port(IFACE_CAN)


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

    def get_serial(self) -> str:
        """Eindeutige Board-ID (STM32-UID-basiert) aus *IDN? (Feld SN=...).

        Identisch mit der USB-Seriennummer (unter Windows als SER=... in der
        hwid sichtbar) - kann genutzt werden, um ein bestimmtes physisches
        Board wiederzuerkennen, z.B. um bekannte Hardware-Defekte je
        Exemplar auszublenden (siehe docs/hardware-notes.md).
        """
        for field in self.idn().split(","):
            if field.startswith("SN="):
                return field[len("SN="):]
        raise MicroHILError("*IDN? enthaelt kein SN=-Feld (alte Firmware?)")

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

    def set_aout_raw_mv(self, n: int, millivolt: int) -> None:
        """Unkalibrierter DAC-Sollwert (0..3300), fuer Kalibrierung/Diagnose - siehe docs/calibration.md."""
        self._command(f"AOUTRAW {n} {millivolt}")

    def get_ain_mv(self, n: int) -> int:
        return int(self._command(f"AIN? {n}"))

    def get_ain_raw_mv(self, n: int) -> int:
        """Unkalibrierter ADC-Rohwert in mV, fuer Kalibrierung/Diagnose - siehe docs/calibration.md."""
        return int(self._command(f"AINRAW? {n}"))

    def set_pwr12(self, n: int, state: bool) -> None:
        self._command(f"PWR12 {n} {int(state)}")

    def get_pwr12(self, n: int) -> bool:
        return self._command(f"PWR12? {n}") == "1"

    def get_pwr12_fault(self, n: int) -> int:
        """Bitmaske: Bit0 = eigenes Ueberstromlimit ausgeloest, Bit1 =
        gemeinsames 1,5A-Eingangsbudget ausgeloest (0 = kein Fault). Erklaert,
        warum PWR12? trotz gesetzter Schaltanforderung 0 liefert - siehe
        docs/protocol.md, Abschnitt "Strombegrenzung"."""
        return int(self._command(f"PWR12FLT? {n}"))

    def set_curr_limit_ma(self, n: int, milliamps: int) -> None:
        """Stromlimit fuer PWR12-1/2, Default nach Reset 1200 mA. Bei
        Ueberschreitung >100ms schaltet die Firmware den Kanal ab und
        verriegelt ihn, bis die Schaltanforderung per set_pwr12(n, False)
        einmal zurueckgenommen wurde (siehe docs/protocol.md)."""
        self._command(f"ILIM {n} {milliamps}")

    def get_curr_limit_ma(self, n: int) -> int:
        return int(self._command(f"ILIM? {n}"))

    def get_curr_ma(self, n: int) -> int:
        return int(self._command(f"CURR? {n}"))

    def get_curr_raw_mv(self, n: int) -> int:
        """Unkalibrierte Sense-Spannung in mV, fuer Kalibrierung/Diagnose - siehe docs/calibration.md."""
        return int(self._command(f"CURRRAW? {n}"))

    def set_pwm(self, n: int, duty_permille: int) -> None:
        """PWM-Kanal 1-4 (PC6-9). Verriegelt mit OUT 1-4 (siehe docs/protocol.md)."""
        self._command(f"PWM {n} {duty_permille}")

    def get_pwm(self, n: int) -> int:
        return int(self._command(f"PWM? {n}"))

    def set_pwm_freq_hz(self, hz: int) -> None:
        """Frequenz fuer PWM1-4 gemeinsam (ein Timer, siehe docs/protocol.md)."""
        self._command(f"PWMFREQ {hz}")

    def get_pwm_freq_hz(self) -> int:
        return int(self._command("PWMFREQ?"))
