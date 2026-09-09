# microHIL

A small, self-built hardware-in-the-loop box: switchable relays, digital and
analog I/O, two current-monitored 12 V supply outputs and a CAN interface —
all driven from a PC over a single USB cable.

microHIL is built around an **STM32F446RET** (LQFP64) on a custom PCB. It
enumerates as a USB composite device with **two virtual COM ports**: one
carries a plain ASCII command protocol for the I/O, the other exposes CAN1 as
a standard SLCAN adapter. Both can be used at the same time.

> **Status:** working prototype, in active use on the bench. The firmware,
> the command protocol and the Python host library are stable and verified
> against real hardware; the PCB is at revision v0.1 and has a few documented
> quirks that needed rework (see [`docs/hardware-notes.md`](docs/hardware-notes.md)).
> Project documentation under `docs/` is currently in German.

## Features

| Function | Channels | Notes |
|---|---|---|
| Relays | 4 | dry contacts |
| Digital outputs | 8 | `OUT1-8` |
| Digital inputs | 8 | 12 V level, comparator front end with selectable 5 V/12 V threshold |
| Analog inputs | 4 | calibrated, reported in mV (divider for up to ~13 V) |
| Analog outputs | 2 | DAC + buffer, calibrated, ~0…12.2 V |
| Switchable 12 V outputs | 2 | high-side switch with current sense (mA) and firmware current limiting |
| PWM outputs | 4 | 0…1000 ‰, configurable frequency 1 Hz…20 kHz (`PWMFREQ`, shared by all 4 channels, ceiling scope-verified), shares the output stage with `OUT1-4` (interlocked in firmware) |
| CAN | 1 (+1 reserved) | CAN1 as SLCAN USB adapter; CAN2 reserved for remote control |

Analog channels (`AIN1-4`, `AOUT1-2`, `CURR1-2`) are individually two-point
calibrated against reference instruments, so the protocol speaks physical
units (mV / mA) rather than raw ADC counts — see
[`docs/calibration.md`](docs/calibration.md).

The 12 V outputs are protected in firmware: a per-channel limit (`ILIM`,
default 1200 mA) and a combined 1500 mA budget for the shared polyfuse. A
tripped channel latches off until its request is explicitly withdrawn;
`PWR12FLT?` reports the cause.

## Repository layout

```
firmware/microHIL_fw/   STM32CubeMX + CMake project for the STM32F446RET
host/                   Python client library, test GUI, hardware-free checks
hardware/               KiCad 9 schematic, PCB and Gerbers (rev v0.1)
docs/                   Protocol reference, CAN/SLCAN reference, calibration,
                        hardware notes (German)
```

## USB interfaces

microHIL presents two CDC-ACM functions under the same VID:PID `0483:5740`
(ST's factory VCP IDs — no own USB vendor ID). They are told apart by
interface number, which `host/microhil.py` does automatically:

| Interface | Windows | Linux | Content |
|---|---|---|---|
| 0 | `…&MI_00` | `…-if00` | HIL command protocol — [`docs/protocol.md`](docs/protocol.md) |
| 2 | `…&MI_02` | `…-if02` | CAN1 as SLCAN — [`docs/can-usb.md`](docs/can-usb.md) |

Two ports rather than one multiplexed port, because a COM port can only be
opened once on Windows: this way the test GUI and a CAN tool can run
side by side.

## Command protocol

Line-based ASCII, uppercase, `\n`-terminated requests, `\r\n`-terminated
replies — a plain `readline()` is enough. Values are always integers in
mV/mA.

```
*IDN?                 -> microHIL,fw=0.1.0,SN=2065386A5631
RELAY 1 1             -> OK
IN?                   -> 10110001        (IN1 first)
AIN? 2                -> 3300            (mV, calibrated)
PWR12 1 1             -> OK
CURR? 1               -> 536             (mA)
PWM 1 500             -> OK              (‰ duty cycle)
```

Errors are exactly `ERR ARGS`, `ERR RANGE` or `ERR UNKNOWN`. The full
reference, including the range-checking rules and the board serial number in
`*IDN?`, is in [`docs/protocol.md`](docs/protocol.md).

CAN1 speaks the SLCAN/Lawicel dialect (CAN232-compatible), so it works with
`python-can` (`interface="slcan"`) and, on Linux, as a regular SocketCAN
interface via `slcand`. Two extensions beyond CAN232: `B<bit/s>` for exact
non-standard bitrates and `Y` for a loopback self-test without a bus. See
[`docs/can-usb.md`](docs/can-usb.md).

## Getting started (host)

```bash
pip install -r host/requirements.txt
```

```python
from host.microhil import MicroHIL

with MicroHIL() as hil:          # finds the right COM port by itself
    print(hil.idn())
    hil.set_relay(1, True)
    print(hil.get_ain_mv(2), "mV")
    hil.set_pwr12(1, True)
    print(hil.get_curr_ma(1), "mA")
```

A PySide6 test GUI that exercises every function manually, with a CAN trace
tab, comes with it:

```bash
python host/gui.py
```

Two checks in `host/tests/` run without any hardware attached — they verify
the CAN bit timing against the bxCAN register limits and the SLCAN wire
format in both directions against the real `python-can` driver:

```bash
python host/tests/check_bit_timing.py
python host/tests/check_slcan_wire.py
```

## Building the firmware

Needs the `arm-none-eabi` GCC toolchain, CMake ≥ 3.22 and Ninja. The project
was generated with STM32CubeMX and is set up for the STM32Cube extension for
VS Code, but builds fine from the command line:

```bash
cd firmware/microHIL_fw
cmake --preset Debug
cmake --build --preset Debug
```

Flashing is over SWD with an ST-Link V2, e.g.:

```bash
STM32_Programmer_CLI -c port=SWD -w build/Debug/microHIL_fw.elf -rst
```

If you regenerate from `microHIL_fw.ioc`, note that the CAN bit timing is
computed at runtime in `Core/Src/slcan.c` (deliberately not stored in CubeMX)
and that the CDC composite build needs ST's `CompositeBuilder`, which CubeMX
does not emit for the F4 series — it is vendored in
`Middlewares/ST/STM32_USB_Device_Library/Class/CompositeBuilder/`.

## Hardware

- MCU: STM32F446RET, LQFP64, 8 MHz crystal, HSE/PLL clock tree
- Debug: SWD (ST-Link V2)
- CAN1: PB8/PB9, transceiver and 120 Ω termination populated
- CAN2: PB5/PB6, not in service yet (see Roadmap)
- KiCad sources, PCB and Gerbers for rev v0.1 are in [`hardware/`](hardware/)

Board revision v0.1 has known issues that required rework — most notably VBUS
routed straight onto the supply rail, and missing pulldowns on the digital
input comparator stages. All of them are documented, with measurements and
the fixes applied, in [`docs/hardware-notes.md`](docs/hardware-notes.md).
Read that file before building a board from these files.

## Roadmap

- **CAN2 as a remote-control interface.** Drive microHIL from a supervising
  ECU or test bench with the same functions as the USB command protocol.
  Frame layout, addressing and bitrate are still to be defined; the bxCAN
  filter bank split needed for CAN2 to receive at all is already in place
  (`SlaveStartFilterBank = 14` in `Core/Src/can_if.c`).
- **CAN1 against a second real bus node**, plus a scope measurement of the
  actual bit rate.

## License

MIT — see [LICENSE](LICENSE).

Vendored third-party code keeps its own license: the STM32 HAL drivers, CMSIS
and the ST USB device library under `firmware/microHIL_fw/Drivers/` and
`firmware/microHIL_fw/Middlewares/` are © STMicroelectronics / Arm and
licensed under their respective terms (see the `LICENSE.txt` files in those
directories).
