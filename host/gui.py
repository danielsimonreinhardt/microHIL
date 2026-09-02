"""Test-GUI fuer microHIL: alle Protokollfunktionen manuell ansteuern."""
import sys

import serial.tools.list_ports
from PySide6.QtCore import QTimer
from PySide6.QtWidgets import (
    QApplication,
    QComboBox,
    QDoubleSpinBox,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QSpinBox,
    QStatusBar,
    QVBoxLayout,
    QWidget,
)

from microhil import VID, PID, MicroHIL, MicroHILError

POLL_INTERVAL_MS = 500


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

        self.din_labels: list[QLabel] = []
        self.ain_labels: list[QLabel] = []
        self.curr_labels: list[QLabel] = []
        self.relay_buttons: list[ToggleButton] = []
        self.out_buttons: list[ToggleButton] = []
        self.pwr12_buttons: list[ToggleButton] = []

        self.setStatusBar(QStatusBar())
        self._build_ui()

        self.poll_timer = QTimer(self)
        self.poll_timer.setInterval(POLL_INTERVAL_MS)
        self.poll_timer.timeout.connect(self._poll_inputs)

        self._set_controls_enabled(False)
        self._refresh_ports()

    # ---------------------------------------------------------------- UI

    def _build_ui(self) -> None:
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)

        root.addWidget(self._build_connection_box())

        row = QHBoxLayout()
        row.addWidget(self._build_relay_box())
        row.addWidget(self._build_out_box())
        row.addWidget(self._build_in_box())
        root.addLayout(row)

        row2 = QHBoxLayout()
        row2.addWidget(self._build_aout_box())
        row2.addWidget(self._build_ain_box())
        row2.addWidget(self._build_pwr12_box())
        root.addLayout(row2)

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
            spin.setRange(0, 3300)
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
            layout.addWidget(QLabel(f"CURR {n} (mV roh)"), i, 2)
            curr_lbl = QLabel("–")
            layout.addWidget(curr_lbl, i, 3)
            self.curr_labels.append(curr_lbl)
        return box

    # --------------------------------------------------------- Verbindung

    def _refresh_ports(self) -> None:
        self.port_combo.clear()
        preferred_index = 0
        for i, p in enumerate(serial.tools.list_ports.comports()):
            label = p.device
            if p.vid == VID and p.pid == PID:
                label += "  (microHIL)"
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
        if self.hil:
            self._guarded(lambda: self.hil.set_out(n, on))

    def _set_pwr12(self, n: int, on: bool) -> None:
        if self.hil:
            self._guarded(lambda: self.hil.set_pwr12(n, on))

    def _set_aout(self, n: int, mv: int) -> None:
        if self.hil:
            self._guarded(lambda: self.hil.set_aout_mv(n, mv))

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
                lbl.setText(str(self.hil.get_curr_mv(i)))

        self._guarded(poll)


def main() -> None:
    app = QApplication(sys.argv)
    window = MicroHILWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
