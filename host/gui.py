"""Test-GUI fuer microHIL: alle Protokollfunktionen manuell ansteuern."""
import sys

import can
import serial.tools.list_ports
from PySide6.QtCore import QTimer
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QDoubleSpinBox,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QSpinBox,
    QStatusBar,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)

from microhil import VID, PID, IFACE_CTRL, MicroHIL, MicroHILError, find_ports
from microhil_can import open_can

POLL_INTERVAL_MS = 500
# CAN-Frames kommen unaufgefordert; bei 500 ms Takt wuerde die Trace-Ansicht
# sichtbar hinterherhinken.
CAN_POLL_INTERVAL_MS = 50
CAN_DRAIN_PER_TICK = 200
CAN_TRACE_LINES = 500

CAN_BITRATES = [10000, 20000, 50000, 83333, 100000, 125000,
                250000, 500000, 800000, 1000000]


def toggle_style(on: bool) -> str:
    return "background-color: #4caf50; color: white;" if on else ""


class ToggleButton(QPushButton):
    """Checkable Button mit AN/AUS-Beschriftung und Farbfeedback."""

    def __init__(self, label: str, on_toggle):
        super().__init__(label)
        self.setCheckable(True)
        self._on_toggle = on_toggle
        self.toggled.connect(self._handle_toggled)
        self._sync_style()

    def _handle_toggled(self, checked: bool) -> None:
        self._sync_style()
        self._on_toggle(checked)

    def _sync_style(self) -> None:
        self.setStyleSheet(toggle_style(self.isChecked()))
        self.setText("AN" if self.isChecked() else "AUS")

    def set_checked_silently(self, checked: bool) -> None:
        self.blockSignals(True)
        self.setChecked(checked)
        self._sync_style()
        self.blockSignals(False)


class MicroHILWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("microHIL Test-GUI")
        self.hil: MicroHIL | None = None
        # CAN haengt am zweiten COM-Port und ist unabhaengig von der
        # HIL-Verbindung -- beides laeuft absichtlich gleichzeitig.
        self.can_bus: can.BusABC | None = None
        self.can_rx_count = 0

        self.din_labels: list[QLabel] = []
        self.ain_labels: list[QLabel] = []
        self.curr_labels: list[QLabel] = []
        self.relay_buttons: list[ToggleButton] = []
        self.out_buttons: list[ToggleButton] = []
        self.pwr12_buttons: list[ToggleButton] = []
        self.pwm_spins: list[QSpinBox] = []

        self.setStatusBar(QStatusBar())
        self._build_ui()

        self.poll_timer = QTimer(self)
        self.poll_timer.setInterval(POLL_INTERVAL_MS)
        self.poll_timer.timeout.connect(self._poll_inputs)

        self.can_timer = QTimer(self)
        self.can_timer.setInterval(CAN_POLL_INTERVAL_MS)
        self.can_timer.timeout.connect(self._poll_can)

        self._set_controls_enabled(False)
        self._refresh_ports()

    def closeEvent(self, event) -> None:
        self._close_can()
        if self.hil is not None:
            self._disconnect()
        super().closeEvent(event)

    # ---------------------------------------------------------------- UI

    def _build_ui(self) -> None:
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)

        root.addWidget(self._build_connection_box())

        # Zwei Reiter, weil HIL-Funktionen und CAN-Trace unabhaengig
        # voneinander benutzt werden -- sie haengen auch an zwei getrennten
        # COM-Ports des Composite-Device.
        tabs = QTabWidget()
        tabs.addTab(self._build_hil_tab(), "HIL")
        tabs.addTab(self._build_can_tab(), "CAN1")
        root.addWidget(tabs)

    def _build_hil_tab(self) -> QWidget:
        page = QWidget()
        layout = QVBoxLayout(page)

        row = QHBoxLayout()
        row.addWidget(self._build_relay_box())
        row.addWidget(self._build_out_box())
        row.addWidget(self._build_in_box())
        layout.addLayout(row)

        row2 = QHBoxLayout()
        row2.addWidget(self._build_aout_box())
        row2.addWidget(self._build_ain_box())
        row2.addWidget(self._build_pwr12_box())
        layout.addLayout(row2)

        layout.addWidget(self._build_pwm_box())
        return page

    def _build_connection_box(self) -> QGroupBox:
        box = QGroupBox("Verbindung")
        layout = QHBoxLayout(box)

        self.port_combo = QComboBox()
        layout.addWidget(self.port_combo)

        refresh_btn = QPushButton("Ports aktualisieren")
        refresh_btn.clicked.connect(self._refresh_ports)
        layout.addWidget(refresh_btn)

        self.connect_btn = QPushButton("Verbinden")
        self.connect_btn.clicked.connect(self._toggle_connection)
        layout.addWidget(self.connect_btn)

        self.idn_label = QLabel("nicht verbunden")
        layout.addWidget(self.idn_label)
        layout.addStretch()
        return box

    def _build_relay_box(self) -> QGroupBox:
        box = QGroupBox("Relais")
        layout = QGridLayout(box)
        for i in range(4):
            n = i + 1
            layout.addWidget(QLabel(f"RELAY {n}"), i, 0)
            btn = ToggleButton("AUS", lambda checked, n=n: self._set_relay(n, checked))
            layout.addWidget(btn, i, 1)
            self.relay_buttons.append(btn)
        return box

    def _build_out_box(self) -> QGroupBox:
        box = QGroupBox("Digitale Ausgänge")
        layout = QGridLayout(box)
        for i in range(8):
            n = i + 1
            layout.addWidget(QLabel(f"OUT {n}"), i, 0)
            btn = ToggleButton("AUS", lambda checked, n=n: self._set_out(n, checked))
            layout.addWidget(btn, i, 1)
            self.out_buttons.append(btn)
        return box

    def _build_in_box(self) -> QGroupBox:
        box = QGroupBox("Digitale Eingänge")
        layout = QGridLayout(box)
        for i in range(8):
            n = i + 1
            layout.addWidget(QLabel(f"IN {n}"), i, 0)
            lbl = QLabel("–")
            layout.addWidget(lbl, i, 1)
            self.din_labels.append(lbl)
        return box

    def _build_aout_box(self) -> QGroupBox:
        box = QGroupBox("Analogausgänge (DAC)")
        layout = QGridLayout(box)
        for i in range(2):
            n = i + 1
            layout.addWidget(QLabel(f"AOUT {n} (mV)"), i, 0)
            spin = QSpinBox()
            # Kalibrierter physikalischer Ausgangsbereich, siehe cal_aout in
            # firmware/microHIL_fw/Core/Src/calibration.c (nominal ~0..12210 mV,
            # bis zur echten Kalibrierung).
            spin.setRange(0, 12210)
            spin.setSingleStep(50)
            spin.valueChanged.connect(lambda mv, n=n: self._set_aout(n, mv))
            layout.addWidget(spin, i, 1)
        return box

    def _build_ain_box(self) -> QGroupBox:
        box = QGroupBox("Analogeingänge")
        layout = QGridLayout(box)
        for i in range(4):
            n = i + 1
            layout.addWidget(QLabel(f"AIN {n}"), i, 0)
            lbl = QLabel("– mV")
            layout.addWidget(lbl, i, 1)
            self.ain_labels.append(lbl)
        return box

    def _build_pwr12_box(self) -> QGroupBox:
        box = QGroupBox("12V-Ausgänge")
        layout = QGridLayout(box)
        for i in range(2):
            n = i + 1
            layout.addWidget(QLabel(f"PWR12 {n}"), i, 0)
            btn = ToggleButton("AUS", lambda checked, n=n: self._set_pwr12(n, checked))
            layout.addWidget(btn, i, 1)
            self.pwr12_buttons.append(btn)
            layout.addWidget(QLabel(f"CURR {n} (mA)"), i, 2)
            curr_lbl = QLabel("–")
            layout.addWidget(curr_lbl, i, 3)
            self.curr_labels.append(curr_lbl)
        return box

    def _build_pwm_box(self) -> QGroupBox:
        box = QGroupBox("PWM (PC6-9, verriegelt mit OUT 1-4)")
        layout = QGridLayout(box)
        for i in range(4):
            n = i + 1
            layout.addWidget(QLabel(f"PWM {n} (‰)"), 0, i * 2)
            spin = QSpinBox()
            spin.setRange(0, 1000)
            spin.setSingleStep(50)
            spin.valueChanged.connect(lambda permille, n=n: self._set_pwm(n, permille))
            layout.addWidget(spin, 0, i * 2 + 1)
            self.pwm_spins.append(spin)

        layout.addWidget(QLabel("Frequenz (Hz, gilt fuer alle 4 Kanaele)"), 1, 0, 1, 6)
        self.pwm_freq_spin = QSpinBox()
        self.pwm_freq_spin.setRange(1, 1_000_000)
        self.pwm_freq_spin.setSingleStep(100)
        self.pwm_freq_spin.setValue(1098)
        self.pwm_freq_spin.valueChanged.connect(self._set_pwm_freq)
        layout.addWidget(self.pwm_freq_spin, 1, 6, 1, 2)
        return box

    # -------------------------------------------------------------- CAN1

    def _build_can_tab(self) -> QWidget:
        page = QWidget()
        layout = QVBoxLayout(page)

        ctrl = QGroupBox("Bus")
        ctrl_layout = QHBoxLayout(ctrl)

        ctrl_layout.addWidget(QLabel("Bitrate"))
        self.can_bitrate_combo = QComboBox()
        for br in CAN_BITRATES:
            self.can_bitrate_combo.addItem(f"{br / 1000:g} kbit/s", br)
        self.can_bitrate_combo.setCurrentIndex(CAN_BITRATES.index(500000))
        ctrl_layout.addWidget(self.can_bitrate_combo)

        self.can_listen_check = QCheckBox("nur mithören")
        self.can_listen_check.setToolTip(
            "Listen-Only: der Controller sendet keine Bits, auch keine ACKs."
        )
        ctrl_layout.addWidget(self.can_listen_check)

        self.can_open_btn = QPushButton("CAN öffnen")
        self.can_open_btn.clicked.connect(self._toggle_can)
        ctrl_layout.addWidget(self.can_open_btn)

        self.can_status_label = QLabel("geschlossen")
        ctrl_layout.addWidget(self.can_status_label)
        ctrl_layout.addStretch()
        layout.addWidget(ctrl)

        send = QGroupBox("Senden")
        send_layout = QHBoxLayout(send)
        send_layout.addWidget(QLabel("ID (hex)"))
        self.can_id_edit = QLineEdit("7DF")
        self.can_id_edit.setMaximumWidth(90)
        send_layout.addWidget(self.can_id_edit)

        self.can_ext_check = QCheckBox("29 bit")
        send_layout.addWidget(self.can_ext_check)
        self.can_rtr_check = QCheckBox("RTR")
        send_layout.addWidget(self.can_rtr_check)

        send_layout.addWidget(QLabel("Daten (hex)"))
        self.can_data_edit = QLineEdit("02 01 00")
        send_layout.addWidget(self.can_data_edit)

        self.can_send_btn = QPushButton("Senden")
        self.can_send_btn.clicked.connect(self._send_can_frame)
        send_layout.addWidget(self.can_send_btn)
        layout.addWidget(send)

        trace = QGroupBox("Trace")
        trace_layout = QVBoxLayout(trace)
        self.can_trace = QPlainTextEdit()
        self.can_trace.setReadOnly(True)
        self.can_trace.setMaximumBlockCount(CAN_TRACE_LINES)
        self.can_trace.setStyleSheet("font-family: Consolas, monospace;")
        trace_layout.addWidget(self.can_trace)

        trace_buttons = QHBoxLayout()
        clear_btn = QPushButton("Leeren")
        clear_btn.clicked.connect(self.can_trace.clear)
        trace_buttons.addWidget(clear_btn)
        self.can_count_label = QLabel("0 Frames")
        trace_buttons.addWidget(self.can_count_label)
        trace_buttons.addStretch()
        trace_layout.addLayout(trace_buttons)
        layout.addWidget(trace)

        self._set_can_controls_enabled(False)
        return page

    def _set_can_controls_enabled(self, open_: bool) -> None:
        self.can_send_btn.setEnabled(open_)
        self.can_bitrate_combo.setEnabled(not open_)
        self.can_listen_check.setEnabled(not open_)

    def _toggle_can(self) -> None:
        if self.can_bus is None:
            self._open_can()
        else:
            self._close_can()

    def _open_can(self) -> None:
        bitrate = self.can_bitrate_combo.currentData()
        listen_only = self.can_listen_check.isChecked()
        try:
            self.can_bus = open_can(bitrate, listen_only=listen_only)
        except (can.CanError, OSError, RuntimeError, ValueError, SystemExit) as exc:
            QMessageBox.critical(self, "microHIL CAN1", f"CAN öffnen fehlgeschlagen:\n{exc}")
            self.can_bus = None
            return

        self.can_rx_count = 0
        self.can_status_label.setText(
            f"offen, {bitrate / 1000:g} kbit/s" + (", listen-only" if listen_only else "")
        )
        self.can_open_btn.setText("CAN schließen")
        self._set_can_controls_enabled(True)
        self.can_timer.start()

    def _close_can(self) -> None:
        self.can_timer.stop()
        if self.can_bus is not None:
            try:
                self.can_bus.shutdown()
            except (can.CanError, OSError):
                pass
            self.can_bus = None
        self.can_status_label.setText("geschlossen")
        self.can_open_btn.setText("CAN öffnen")
        self._set_can_controls_enabled(False)

    def _send_can_frame(self) -> None:
        if self.can_bus is None:
            return
        try:
            can_id = int(self.can_id_edit.text().strip(), 16)
            data = bytes.fromhex(self.can_data_edit.text().replace(" ", ""))
        except ValueError as exc:
            self.statusBar().showMessage(f"Ungültige Eingabe: {exc}", 5000)
            return
        if len(data) > 8:
            self.statusBar().showMessage("Höchstens 8 Datenbytes", 5000)
            return

        msg = can.Message(
            arbitration_id=can_id,
            is_extended_id=self.can_ext_check.isChecked(),
            is_remote_frame=self.can_rtr_check.isChecked(),
            data=data,
        )
        try:
            self.can_bus.send(msg)
        except (can.CanError, OSError) as exc:
            self.statusBar().showMessage(f"CAN-Sendefehler: {exc}", 5000)
            return
        self.can_trace.appendPlainText(f"TX  {self._format_frame(msg)}")

    @staticmethod
    def _format_frame(msg: can.Message) -> str:
        ident = f"{msg.arbitration_id:08X}" if msg.is_extended_id else f"{msg.arbitration_id:03X}"
        if msg.is_remote_frame:
            payload = f"RTR dlc={msg.dlc}"
        else:
            payload = " ".join(f"{b:02X}" for b in msg.data)
        return f"{ident:>8}  [{msg.dlc}]  {payload}"

    def _poll_can(self) -> None:
        if self.can_bus is None:
            return
        try:
            for _ in range(CAN_DRAIN_PER_TICK):
                msg = self.can_bus.recv(0)
                if msg is None:
                    break
                self.can_rx_count += 1
                self.can_trace.appendPlainText(f"RX  {self._format_frame(msg)}")
        except (can.CanError, OSError) as exc:
            self.statusBar().showMessage(f"CAN-Fehler: {exc}", 5000)
            self._close_can()
            return
        self.can_count_label.setText(f"{self.can_rx_count} Frames")

    # --------------------------------------------------------- Verbindung

    def _refresh_ports(self) -> None:
        self.port_combo.clear()
        # Seit dem Composite-Device meldet microHIL zwei Ports mit derselben
        # VID/PID. Der Steuerport (Interface 0) muss vorausgewaehlt werden,
        # sonst landet die GUI auf dem CAN-Port.
        ctrl_port = find_ports().get(IFACE_CTRL)
        preferred_index = 0
        for i, p in enumerate(serial.tools.list_ports.comports()):
            label = p.device
            if p.vid == VID and p.pid == PID:
                label += "  (microHIL CAN1)" if p.device != ctrl_port else "  (microHIL)"
                if p.device == ctrl_port:
                    preferred_index = i
            self.port_combo.addItem(label, p.device)
        self.port_combo.setCurrentIndex(preferred_index)

    def _toggle_connection(self) -> None:
        if self.hil is None:
            self._connect()
        else:
            self._disconnect()

    def _connect(self) -> None:
        port = self.port_combo.currentData()
        if not port:
            QMessageBox.warning(self, "microHIL", "Kein Port ausgewählt.")
            return
        try:
            self.hil = MicroHIL(port=port, timeout=0.5)
            idn = self.hil.idn()
        except (MicroHILError, OSError) as exc:
            QMessageBox.critical(self, "microHIL", f"Verbindung fehlgeschlagen:\n{exc}")
            self.hil = None
            return

        self.idn_label.setText(idn)
        self.connect_btn.setText("Trennen")
        self._set_controls_enabled(True)
        self.poll_timer.start()
        self.statusBar().showMessage(f"Verbunden mit {port}", 3000)

    def _disconnect(self) -> None:
        self.poll_timer.stop()
        if self.hil is not None:
            self.hil.close()
            self.hil = None
        self.idn_label.setText("nicht verbunden")
        self.connect_btn.setText("Verbinden")
        self._set_controls_enabled(False)

    def _set_controls_enabled(self, enabled: bool) -> None:
        for btn in [*self.relay_buttons, *self.out_buttons, *self.pwr12_buttons]:
            btn.setEnabled(enabled)
        for box in self.findChildren(QSpinBox):
            box.setEnabled(enabled)

    # ------------------------------------------------------------- Aktionen

    def _guarded(self, fn) -> None:
        try:
            fn()
        except (MicroHILError, OSError) as exc:
            self.statusBar().showMessage(f"Fehler: {exc}", 5000)
            self._disconnect()

    def _set_relay(self, n: int, on: bool) -> None:
        if self.hil:
            self._guarded(lambda: self.hil.set_relay(n, on))

    def _set_out(self, n: int, on: bool) -> None:
        if on and n <= 4:
            # Verriegelt mit PWM n auf der Firmware, hier nur die Anzeige nachziehen
            spin = self.pwm_spins[n - 1]
            spin.blockSignals(True)
            spin.setValue(0)
            spin.blockSignals(False)
        if self.hil:
            self._guarded(lambda: self.hil.set_out(n, on))

    def _set_pwr12(self, n: int, on: bool) -> None:
        if self.hil:
            self._guarded(lambda: self.hil.set_pwr12(n, on))

    def _set_aout(self, n: int, mv: int) -> None:
        if self.hil:
            self._guarded(lambda: self.hil.set_aout_mv(n, mv))

    def _set_pwm(self, n: int, permille: int) -> None:
        if permille > 0:
            # Verriegelt mit OUT n auf der Firmware, hier nur die Anzeige nachziehen
            self.out_buttons[n - 1].set_checked_silently(False)
        if self.hil:
            self._guarded(lambda: self.hil.set_pwm(n, permille))

    def _set_pwm_freq(self, hz: int) -> None:
        if self.hil:
            self._guarded(lambda: self.hil.set_pwm_freq_hz(hz))

    # -------------------------------------------------------------- Polling

    def _poll_inputs(self) -> None:
        if self.hil is None:
            return

        def poll():
            bits = self.hil.get_in_all()
            for lbl, bit in zip(self.din_labels, bits):
                lbl.setText("1" if bit else "0")
                lbl.setStyleSheet(toggle_style(bit))

            for i, lbl in enumerate(self.ain_labels, start=1):
                lbl.setText(f"{self.hil.get_ain_mv(i)} mV")

            for i, lbl in enumerate(self.curr_labels, start=1):
                lbl.setText(f"{self.hil.get_curr_ma(i)} mA")

        self._guarded(poll)


def main() -> None:
    app = QApplication(sys.argv)
    window = MicroHILWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
