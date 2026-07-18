"""Real-time C12880MA desktop workbench."""

from __future__ import annotations

from collections import deque
import csv
from importlib import resources
from pathlib import Path
import threading
import time

import numpy as np
import pyqtgraph as pg
import pyqtgraph.exporters
from PySide6 import QtCore, QtGui, QtWidgets

from .calibration import WavelengthCalibration, metrics, process_counts
from .device import C12880Device, enumerate_ports, port_report
from .protocol import DEFAULT_EXPOSURE_MS, DEFAULT_OUTPUT_MASK, SpectrumFrame
from .recording import CsvRecorder

PROJECT_ROOT = Path(__file__).resolve().parents[2]


class LatestFrame:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._frame: SpectrumFrame | None = None
        self._generation = 0

    def publish(self, frame: SpectrumFrame) -> None:
        with self._lock:
            self._frame = frame
            self._generation += 1

    def snapshot(self) -> tuple[int, SpectrumFrame | None]:
        with self._lock:
            return self._generation, self._frame


class SharedControls:
    def __init__(self, exposure_ms: float = DEFAULT_EXPOSURE_MS) -> None:
        self._lock = threading.Lock()
        self._exposure_ms = exposure_ms
        self._averaging = 1
        self._trigger_mode = "internal"
        self._output_mask = DEFAULT_OUTPUT_MASK

    def update(
        self, exposure_ms: float, averaging: int, trigger_mode: str, output_mask: int
    ) -> None:
        with self._lock:
            self._exposure_ms = float(exposure_ms)
            self._averaging = max(1, int(averaging))
            self._trigger_mode = trigger_mode
            self._output_mask = int(output_mask)

    def snapshot(self) -> tuple[float, int, str, int]:
        with self._lock:
            return (
                self._exposure_ms,
                self._averaging,
                self._trigger_mode,
                self._output_mask,
            )


class AcquisitionWorker(QtCore.QObject):
    status = QtCore.Signal(object)
    finished = QtCore.Signal()

    def __init__(
        self,
        latest: LatestFrame,
        controls: SharedControls,
        recorder: CsvRecorder,
        requested_port: str | None,
    ) -> None:
        super().__init__()
        self.latest = latest
        self.controls = controls
        self.recorder = recorder
        self.device = C12880Device(requested_port)
        self.stop_event = threading.Event()

    def request_stop(self) -> None:
        self.stop_event.set()

    @QtCore.Slot()
    def run(self) -> None:
        valid = 0
        invalid = 0
        t0 = time.perf_counter()
        last_status = 0.0
        window: deque[np.ndarray] = deque(maxlen=1)
        window_sum: np.ndarray | None = None
        try:
            descriptor = self.device.connect()
            diagnostics = self.device.diagnostic_report()
            self.status.emit({
                "state": "connected",
                "message": (
                    f"Transport open on {descriptor.device}; "
                    f"identity={diagnostics['identity_valid']} "
                    f"correction={diagnostics['correction_bytes']} B"
                ),
                "port": descriptor.to_dict(),
                "valid": valid,
                "invalid": invalid,
                "fps": 0.0,
            })
            while not self.stop_event.is_set():
                exposure_ms, averaging, trigger_mode, output_mask = self.controls.snapshot()
                self.device.set_trigger_mode(trigger_mode)  # type: ignore[arg-type]
                self.device.set_output_mask(output_mask)
                if window.maxlen != averaging:
                    window = deque(maxlen=averaging)
                    window_sum = None
                try:
                    raw_frame = self.device.acquire(exposure_ms)
                    self.recorder.write(raw_frame)
                    if averaging == 1:
                        frame = raw_frame
                    else:
                        counts64 = raw_frame.counts.astype(np.float64, copy=False)
                        if len(window) == window.maxlen:
                            assert window_sum is not None
                            window_sum -= window[0]
                        window.append(counts64)
                        if window_sum is None:
                            window_sum = counts64.copy()
                        else:
                            window_sum += counts64
                        frame = SpectrumFrame(
                            sequence=raw_frame.sequence,
                            timestamp_ns=raw_frame.timestamp_ns,
                            exposure_ms=raw_frame.exposure_ms,
                            counts=window_sum / len(window),
                            prefix_words=raw_frame.prefix_words,
                            raw_size=raw_frame.raw_size,
                            source=raw_frame.source,
                        )
                    self.latest.publish(frame)
                    valid += 1
                    state = "streaming"
                    message = "Valid 288-pixel stream"
                except Exception as exc:
                    invalid += 1
                    state = "fault"
                    raw = self.device.last_raw
                    message = f"{type(exc).__name__}: {exc}; raw={len(raw)} B, nonzero={sum(bool(v) for v in raw)}"
                    time.sleep(0.08)
                now = time.perf_counter()
                if now - last_status >= 0.45:
                    elapsed = max(now - t0, 1e-9)
                    self.status.emit({
                        "state": state,
                        "message": message,
                        "port": self.device.descriptor.to_dict() if self.device.descriptor else None,
                        "valid": valid,
                        "invalid": invalid,
                        "fps": valid / elapsed,
                        "raw_hex": self.device.last_raw[:48].hex(" "),
                    })
                    last_status = now
        except Exception as exc:
            self.status.emit({
                "state": "fault", "message": f"{type(exc).__name__}: {exc}",
                "port": None, "valid": valid, "invalid": invalid, "fps": 0.0,
            })
        finally:
            self.device.close()
            self.recorder.stop()
            self.finished.emit()


class MetricCard(QtWidgets.QFrame):
    def __init__(self, label: str, value: str, accent: str) -> None:
        super().__init__()
        self.setObjectName("metricCard")
        layout = QtWidgets.QVBoxLayout(self)
        layout.setContentsMargins(15, 12, 15, 12)
        layout.setSpacing(3)
        title = QtWidgets.QLabel(label.upper())
        title.setObjectName("metricLabel")
        self.value = QtWidgets.QLabel(value)
        self.value.setObjectName("metricValue")
        self.value.setStyleSheet(f"color: {accent};")
        layout.addWidget(title)
        layout.addWidget(self.value)


class SpectrumWindow(QtWidgets.QMainWindow):
    def __init__(self, *, requested_port: str | None = None, demo_only: bool = False) -> None:
        super().__init__()
        self.requested_port = requested_port
        self.demo_only = demo_only
        self.calibration = WavelengthCalibration.bundled()
        self.wavelengths = self.calibration.wavelengths()
        self.latest = LatestFrame()
        self.controls = SharedControls()
        self.recorder = CsvRecorder(self.wavelengths)
        self.thread: QtCore.QThread | None = None
        self.worker: AcquisitionWorker | None = None
        self.last_generation = -1
        self.current_frame: SpectrumFrame | None = None
        self.current_counts: np.ndarray | None = None
        self.dark_reference: np.ndarray | None = None
        self.y_floor = 0.0
        self.y_ceiling = 1.0
        self.setWindowTitle("AgInTi Spectrum Studio")
        self.resize(1480, 900)
        self.setMinimumSize(1100, 700)
        self._build_ui()
        self._apply_style()
        if demo_only:
            self._load_reference_sample(initial=True)
        else:
            self._show_waiting_state()
        self.timer = QtCore.QTimer(self)
        self.timer.setInterval(33)
        self.timer.timeout.connect(self._refresh_plot)
        self.timer.start()
        if not demo_only:
            QtCore.QTimer.singleShot(350, self.start_hardware)

    def _build_ui(self) -> None:
        central = QtWidgets.QWidget()
        root = QtWidgets.QVBoxLayout(central)
        root.setContentsMargins(22, 18, 22, 20)
        root.setSpacing(14)

        header = QtWidgets.QHBoxLayout()
        brand = QtWidgets.QVBoxLayout()
        title = QtWidgets.QLabel("SPECTRUM STUDIO")
        title.setObjectName("brandTitle")
        subtitle = QtWidgets.QLabel("C12880MA / 288 pixels / acquisition decoupled from rendering")
        subtitle.setObjectName("brandSubtitle")
        brand.addWidget(title)
        brand.addWidget(subtitle)
        header.addLayout(brand)
        header.addStretch(1)
        self.status_pill = QtWidgets.QLabel("REFERENCE")
        self.status_pill.setObjectName("statusPill")
        self.port_label = QtWidgets.QLabel("CH343: scanning")
        self.port_label.setObjectName("portLabel")
        self.connect_button = QtWidgets.QPushButton("Connect hardware")
        self.connect_button.clicked.connect(self.start_hardware)
        header.addWidget(self.port_label)
        header.addWidget(self.status_pill)
        header.addWidget(self.connect_button)
        root.addLayout(header)

        splitter = QtWidgets.QSplitter(QtCore.Qt.Orientation.Horizontal)
        splitter.setChildrenCollapsible(False)
        plot_panel = QtWidgets.QFrame()
        plot_panel.setObjectName("plotPanel")
        plot_layout = QtWidgets.QVBoxLayout(plot_panel)
        plot_layout.setContentsMargins(16, 16, 16, 12)
        self.plot = pg.PlotWidget(background="#fffdf8")
        self.plot.setLabel("bottom", "Wavelength", units="nm")
        self.plot.setLabel("left", "ADC counts")
        self.plot.showGrid(x=True, y=True, alpha=0.18)
        self.plot.setXRange(340, 850, padding=0)
        self.plot.setMouseEnabled(x=True, y=False)
        self.plot.getAxis("bottom").setPen(pg.mkPen("#7e8a8d"))
        self.plot.getAxis("left").setPen(pg.mkPen("#7e8a8d"))
        self.plot.getAxis("bottom").setTextPen(pg.mkPen("#394b50"))
        self.plot.getAxis("left").setTextPen(pg.mkPen("#394b50"))
        for left, right, color in [
            (340, 380, (117, 103, 216, 16)), (380, 450, (77, 85, 217, 16)),
            (450, 495, (23, 135, 209, 16)), (495, 570, (50, 168, 107, 16)),
            (570, 590, (217, 182, 37, 18)), (590, 620, (237, 123, 47, 18)),
            (620, 700, (217, 66, 66, 16)), (700, 850, (142, 81, 81, 12)),
        ]:
            region = pg.LinearRegionItem((left, right), movable=False, brush=color, pen=None)
            region.setZValue(-20)
            self.plot.addItem(region)
        self.curve = self.plot.plot(
            self.wavelengths, np.zeros_like(self.wavelengths),
            pen=pg.mkPen("#007f76", width=2.4),
            fillLevel=0, brush=pg.mkBrush(42, 166, 154, 35),
        )
        self.peak_marker = pg.ScatterPlotItem(size=11, brush="#ff6542", pen=pg.mkPen("#ffffff", width=1.5))
        self.plot.addItem(self.peak_marker)
        self.reference_note = pg.TextItem(
            "VENDOR REFERENCE / NOMINAL AXIS", color="#9b351f", anchor=(0, 0)
        )
        self.reference_note.setFont(QtGui.QFont("Bahnschrift", 10, QtGui.QFont.Weight.Bold))
        self.reference_note.setPos(355, 1)
        self.plot.addItem(self.reference_note)
        plot_layout.addWidget(self.plot, 1)
        self.plot_caption = QtWidgets.QLabel(
            "Reference workbook shown while hardware waits for a complete frame."
        )
        self.plot_caption.setObjectName("plotCaption")
        plot_layout.addWidget(self.plot_caption)
        splitter.addWidget(plot_panel)

        side = QtWidgets.QWidget()
        side.setMinimumWidth(370)
        side.setMaximumWidth(460)
        side_layout = QtWidgets.QVBoxLayout(side)
        side_layout.setContentsMargins(4, 0, 0, 0)
        side_layout.setSpacing(11)
        cards = QtWidgets.QGridLayout()
        cards.setSpacing(9)
        self.peak_card = MetricCard("Peak", "-- nm", "#007f76")
        self.signal_card = MetricCard("Peak signal", "--", "#d64f32")
        self.integral_card = MetricCard("Integral", "--", "#2d5d80")
        self.rate_card = MetricCard("Acquisition", "-- fps", "#80611a")
        cards.addWidget(self.peak_card, 0, 0)
        cards.addWidget(self.signal_card, 0, 1)
        cards.addWidget(self.integral_card, 1, 0)
        cards.addWidget(self.rate_card, 1, 1)
        side_layout.addLayout(cards)

        controls = QtWidgets.QFrame()
        controls.setObjectName("controlPanel")
        form = QtWidgets.QFormLayout(controls)
        form.setContentsMargins(16, 15, 16, 15)
        form.setSpacing(12)
        self.exposure = QtWidgets.QDoubleSpinBox()
        self.exposure.setRange(3.0, 10.0)
        self.exposure.setDecimals(3)
        self.exposure.setValue(DEFAULT_EXPOSURE_MS * 1_000.0)
        self.exposure.setSingleStep(0.1)
        self.exposure.valueChanged.connect(self._controls_changed)
        self._exposure_display_unit = "us"
        self.exposure_unit = QtWidgets.QComboBox()
        self.exposure_unit.addItem("us", "us")
        self.exposure_unit.addItem("ms", "ms")
        self.exposure_unit.setFixedWidth(64)
        self.exposure_unit.currentIndexChanged.connect(self._exposure_unit_changed)
        integration_control = QtWidgets.QWidget()
        integration_layout = QtWidgets.QHBoxLayout(integration_control)
        integration_layout.setContentsMargins(0, 0, 0, 0)
        integration_layout.setSpacing(6)
        integration_layout.addWidget(self.exposure, 1)
        integration_layout.addWidget(self.exposure_unit)
        self.averaging = QtWidgets.QSpinBox()
        self.averaging.setRange(1, 64)
        self.averaging.setValue(1)
        self.averaging.valueChanged.connect(self._controls_changed)
        self.trigger_mode = QtWidgets.QComboBox()
        self.trigger_mode.addItem("Internal / software", "internal")
        self.trigger_mode.addItem("External / TTL", "external")
        self.trigger_mode.currentIndexChanged.connect(self._controls_changed)
        self.output_mask = QtWidgets.QSpinBox()
        self.output_mask.setRange(0, 3)
        self.output_mask.setValue(DEFAULT_OUTPUT_MASK)
        self.output_mask.valueChanged.connect(self._controls_changed)
        form.addRow("Integration", integration_control)
        form.addRow("Frame average", self.averaging)
        form.addRow("Trigger", self.trigger_mode)
        form.addRow("OUT2/OUT3 mask", self.output_mask)

        self.auto_y = QtWidgets.QCheckBox("Auto")
        self.auto_y.setChecked(True)
        self.auto_y.setToolTip(
            "Continuously follow the useful signal range using robust percentiles."
        )
        self.auto_y.toggled.connect(self._auto_y_toggled)
        self.fit_y_button = QtWidgets.QPushButton("Fit current")
        self.fit_y_button.setToolTip(
            "Find the best Y range for the current spectrum, then freeze that range."
        )
        self.fit_y_button.clicked.connect(self._fit_current_y)
        scale_mode_control = QtWidgets.QWidget()
        scale_mode_layout = QtWidgets.QHBoxLayout(scale_mode_control)
        scale_mode_layout.setContentsMargins(0, 0, 0, 0)
        scale_mode_layout.setSpacing(8)
        scale_mode_layout.addWidget(self.auto_y)
        scale_mode_layout.addStretch(1)
        scale_mode_layout.addWidget(self.fit_y_button)

        self.y_min_control = QtWidgets.QDoubleSpinBox()
        self.y_min_control.setRange(-1_000_000_000.0, 1_000_000_000.0)
        self.y_min_control.setDecimals(1)
        self.y_min_control.setSingleStep(100.0)
        self.y_min_control.setValue(0.0)
        self.y_min_control.setEnabled(False)
        self.y_min_control.valueChanged.connect(self._manual_y_changed)
        self.y_max_control = QtWidgets.QDoubleSpinBox()
        self.y_max_control.setRange(-999_999_999.0, 1_000_000_000.0)
        self.y_max_control.setDecimals(1)
        self.y_max_control.setSingleStep(100.0)
        self.y_max_control.setValue(65_535.0)
        self.y_max_control.setEnabled(False)
        self.y_max_control.valueChanged.connect(self._manual_y_changed)
        scale_limits_control = QtWidgets.QWidget()
        scale_limits_layout = QtWidgets.QHBoxLayout(scale_limits_control)
        scale_limits_layout.setContentsMargins(0, 0, 0, 0)
        scale_limits_layout.setSpacing(6)
        scale_limits_layout.addWidget(self.y_min_control, 1)
        scale_limits_layout.addWidget(QtWidgets.QLabel("to"))
        scale_limits_layout.addWidget(self.y_max_control, 1)
        form.addRow("Y scale", scale_mode_control)
        form.addRow("Y limits", scale_limits_control)
        side_layout.addWidget(controls)

        actions = QtWidgets.QGridLayout()
        self.dark_button = QtWidgets.QPushButton("Capture dark")
        self.dark_button.clicked.connect(self.capture_dark)
        self.clear_dark_button = QtWidgets.QPushButton("Clear dark")
        self.clear_dark_button.clicked.connect(self.clear_dark)
        self.record_button = QtWidgets.QPushButton("Record raw CSV")
        self.record_button.setCheckable(True)
        self.record_button.toggled.connect(self.toggle_recording)
        self.save_button = QtWidgets.QPushButton("Export plot")
        self.save_button.clicked.connect(self.export_plot)
        self.sample_button = QtWidgets.QPushButton("Load vendor reference")
        self.sample_button.clicked.connect(lambda: self._load_reference_sample(initial=False))
        actions.addWidget(self.dark_button, 0, 0)
        actions.addWidget(self.clear_dark_button, 0, 1)
        actions.addWidget(self.record_button, 1, 0)
        actions.addWidget(self.save_button, 1, 1)
        actions.addWidget(self.sample_button, 2, 0, 1, 2)
        side_layout.addLayout(actions)

        diagnostic_label = QtWidgets.QLabel("LINK DIAGNOSTICS")
        diagnostic_label.setObjectName("sectionLabel")
        self.diagnostics = QtWidgets.QPlainTextEdit()
        self.diagnostics.setReadOnly(True)
        self.diagnostics.setMaximumBlockCount(120)
        self.diagnostics.setMinimumHeight(170)
        self.diagnostics.setObjectName("diagnostics")
        side_layout.addWidget(diagnostic_label)
        side_layout.addWidget(self.diagnostics, 1)
        splitter.addWidget(side)
        splitter.setStretchFactor(0, 1)
        splitter.setSizes([1000, 365])
        root.addWidget(splitter, 1)

        footer = QtWidgets.QHBoxLayout()
        self.frame_label = QtWidgets.QLabel("Frame --")
        self.calibration_label = QtWidgets.QLabel(
            f"Axis: {self.calibration.label} / per-device coefficients not installed"
        )
        self.saturation_label = QtWidgets.QLabel("Saturation --")
        for widget in (self.frame_label, self.calibration_label, self.saturation_label):
            widget.setObjectName("footerLabel")
        footer.addWidget(self.frame_label)
        footer.addStretch(1)
        footer.addWidget(self.calibration_label)
        footer.addStretch(1)
        footer.addWidget(self.saturation_label)
        root.addLayout(footer)
        self.setCentralWidget(central)

    def _apply_style(self) -> None:
        self.setStyleSheet("""
            QMainWindow, QWidget { background: #f4f0e7; color: #20343b; font-family: "Aptos", "Segoe UI"; font-size: 10pt; }
            QLabel#brandTitle { font-family: "Bahnschrift"; font-size: 25pt; font-weight: 700; letter-spacing: 2px; color: #102a35; }
            QLabel#brandSubtitle { color: #647277; font-size: 9pt; }
            QLabel#statusPill { background: #f4c95d; color: #4f3d0b; border-radius: 11px; padding: 6px 12px; font-family: "Bahnschrift"; font-weight: 700; }
            QLabel#portLabel { color: #53666c; font-family: "Cascadia Mono"; font-size: 9pt; }
            QFrame#plotPanel, QFrame#controlPanel, QFrame#metricCard { background: #fffdf8; border: 1px solid #ddd6c9; border-radius: 12px; }
            QLabel#metricLabel, QLabel#sectionLabel { color: #7a8587; font-family: "Bahnschrift"; font-size: 8pt; font-weight: 700; letter-spacing: 1px; }
            QLabel#metricValue { font-family: "Bahnschrift"; font-size: 16pt; font-weight: 700; }
            QLabel#plotCaption, QLabel#footerLabel { color: #677579; font-size: 8.5pt; }
            QPushButton { background: #102a35; color: #fffdf8; border: 0; border-radius: 8px; padding: 8px 12px; font-family: "Bahnschrift"; font-weight: 600; }
            QPushButton:hover { background: #17404e; }
            QPushButton:pressed { background: #071c24; }
            QPushButton:checked { background: #d64f32; }
            QDoubleSpinBox, QSpinBox, QComboBox { background: #f8f5ee; border: 1px solid #d6cec0; border-radius: 7px; padding: 6px; color: #173039; }
            QCheckBox { color: #173039; spacing: 7px; font-family: "Bahnschrift"; font-weight: 600; }
            QPlainTextEdit#diagnostics { background: #102a35; color: #b9e6de; border: 0; border-radius: 10px; padding: 10px; font-family: "Cascadia Mono"; font-size: 8.5pt; }
            QSplitter::handle { background: transparent; width: 8px; }
        """)

    def _displayed_exposure_ms(self) -> float:
        value = float(self.exposure.value())
        return value / 1_000.0 if self.exposure_unit.currentData() == "us" else value

    def _exposure_unit_changed(self, _index: int) -> None:
        new_unit = str(self.exposure_unit.currentData())
        old_unit = self._exposure_display_unit
        if new_unit == old_unit:
            return
        current_ms = (
            float(self.exposure.value()) / 1_000.0
            if old_unit == "us"
            else float(self.exposure.value())
        )
        self._exposure_display_unit = new_unit
        self.exposure.blockSignals(True)
        if new_unit == "us":
            self.exposure.setRange(3.0, 10.0)
            self.exposure.setDecimals(3)
            self.exposure.setSingleStep(0.1)
            self.exposure.setValue(current_ms * 1_000.0)
        else:
            self.exposure.setRange(0.003, 0.010)
            self.exposure.setDecimals(3)
            self.exposure.setSingleStep(0.001)
            self.exposure.setValue(current_ms)
        self.exposure.blockSignals(False)
        self._controls_changed()

    @staticmethod
    def _suggest_y_range(counts: np.ndarray) -> tuple[float, float]:
        finite = counts[np.isfinite(counts)]
        if not finite.size:
            return 0.0, 1.0
        low = float(np.percentile(finite, 1.0))
        high = float(np.percentile(finite, 99.5))
        span = max(1.0, high - low, abs(high) * 0.02)
        floor = max(0.0, low - span * 0.10)
        ceiling = high + span * 0.12
        return floor, max(floor + 1.0, ceiling)

    def _set_y_limit_controls(self, floor: float, ceiling: float) -> None:
        self.y_min_control.blockSignals(True)
        self.y_max_control.blockSignals(True)
        self.y_min_control.setValue(floor)
        self.y_max_control.setValue(max(floor + 1.0, ceiling))
        self.y_min_control.blockSignals(False)
        self.y_max_control.blockSignals(False)

    def _apply_manual_y_range(self) -> None:
        floor = float(self.y_min_control.value())
        ceiling = max(floor + 1.0, float(self.y_max_control.value()))
        if ceiling != self.y_max_control.value():
            self._set_y_limit_controls(floor, ceiling)
        self.y_floor = floor
        self.y_ceiling = ceiling
        self.plot.setYRange(floor, ceiling, padding=0)

    def _auto_y_toggled(self, enabled: bool) -> None:
        self.y_min_control.setEnabled(not enabled)
        self.y_max_control.setEnabled(not enabled)
        if enabled:
            self.y_floor = 0.0
            self.y_ceiling = 1.0
        else:
            self._apply_manual_y_range()

    def _manual_y_changed(self, _value: float) -> None:
        if not self.auto_y.isChecked():
            self._apply_manual_y_range()

    def _fit_current_y(self) -> None:
        if self.current_counts is None:
            return
        floor, ceiling = self._suggest_y_range(self.current_counts)
        self._set_y_limit_controls(floor, ceiling)
        self.auto_y.setChecked(False)
        self._apply_manual_y_range()

    def _controls_changed(self) -> None:
        self.controls.update(
            self._displayed_exposure_ms(),
            self.averaging.value(),
            str(self.trigger_mode.currentData()),
            self.output_mask.value(),
        )

    def _show_waiting_state(self) -> None:
        self.current_frame = None
        self.current_counts = None
        self.curve.setData(self.wavelengths, np.zeros_like(self.wavelengths))
        self.peak_marker.clear()
        self.reference_note.setText("NO VALID HARDWARE FRAME")
        self.reference_note.setPos(355, 0.92)
        self.reference_note.show()
        self.status_pill.setText("WAITING")
        self.plot_caption.setText(
            "No spectrum is plotted until a complete, nonzero 590-byte hardware frame is validated."
        )
        self.peak_card.value.setText("-- nm")
        self.signal_card.value.setText("--")
        self.integral_card.value.setText("--")
        self.frame_label.setText("No validated hardware frame")
        self.saturation_label.setText("Saturation --")

    def _load_reference_sample(self, *, initial: bool) -> None:
        source = resources.files("spectral").joinpath("resources/vendor_sample.csv")
        wavelengths: list[float] = []
        values: list[float] = []
        with source.open("r", encoding="utf-8", newline="") as handle:
            for row in csv.DictReader(handle):
                wavelengths.append(float(row["wavelength_nm"]))
                values.append(float(row["counts"]))
        self.wavelengths = np.asarray(wavelengths)
        self.current_counts = np.asarray(values)
        self.current_frame = SpectrumFrame(
            sequence=-1, timestamp_ns=time.time_ns(), exposure_ms=0.0,
            counts=self.current_counts.copy(), prefix_words=(), raw_size=0,
            source="vendor-reference",
        )
        self._render(self.current_frame, self.current_counts)
        self.reference_note.setText("VENDOR REFERENCE / NOMINAL AXIS")
        self.reference_note.show()
        self.status_pill.setText("REFERENCE")
        self.plot_caption.setText(
            "Vendor workbook data on a nominal linear wavelength axis; not a live measurement."
        )
        if not initial:
            self._append_diagnostic("Loaded vendor reference spectrum.")

    def start_hardware(self) -> None:
        if self.thread and self.thread.isRunning():
            return
        self.connect_button.setEnabled(False)
        self.status_pill.setText("CONNECTING")
        self._append_diagnostic("Scanning serial ports: " + str(port_report(enumerate_ports())))
        self.thread = QtCore.QThread(self)
        self.worker = AcquisitionWorker(
            self.latest, self.controls, self.recorder, self.requested_port
        )
        self.worker.moveToThread(self.thread)
        self.thread.started.connect(self.worker.run)
        self.worker.status.connect(self._hardware_status)
        self.worker.finished.connect(self.thread.quit)
        self.worker.finished.connect(self.worker.deleteLater)
        self.thread.finished.connect(self._thread_finished)
        self.thread.start()

    @QtCore.Slot(object)
    def _hardware_status(self, status: dict[str, object]) -> None:
        state = str(status.get("state", "fault"))
        if state == "streaming":
            self.status_pill.setText("LIVE")
            self.status_pill.setStyleSheet("background:#2ca58d;color:white;")
        elif state == "connected":
            self.status_pill.setText("LINK OPEN")
            self.status_pill.setStyleSheet("background:#e5b84e;color:#4f3d0b;")
        else:
            self.status_pill.setText("FRAME FAULT")
            self.status_pill.setStyleSheet("background:#d64f32;color:white;")
        port = status.get("port")
        if isinstance(port, dict):
            self.port_label.setText(f"{port.get('device')} / {port.get('vid_pid')}")
        self.rate_card.value.setText(f"{float(status.get('fps', 0.0)):.1f} fps")
        self._append_diagnostic(
            f"{state.upper()} valid={status.get('valid', 0)} invalid={status.get('invalid', 0)}\n"
            f"{status.get('message', '')}\nraw: {status.get('raw_hex', '')}"
        )

    def _thread_finished(self) -> None:
        self.connect_button.setEnabled(True)
        self.thread = None
        self.worker = None

    def _refresh_plot(self) -> None:
        generation, frame = self.latest.snapshot()
        if frame is None or generation == self.last_generation:
            return
        self.last_generation = generation
        self.current_frame = frame
        counts = process_counts(frame.counts, dark=self.dark_reference)
        self.current_counts = counts
        self._render(frame, counts)
        self.reference_note.hide()
        self.plot_caption.setText(
            "Live validated frame; acquisition continues independently of this 30 Hz display."
        )

    def _render(self, frame: SpectrumFrame, counts: np.ndarray) -> None:
        self.curve.setData(self.wavelengths, counts)
        summary = metrics(self.wavelengths, counts)
        self.peak_marker.setData([summary.peak_nm], [summary.peak_counts])
        if self.auto_y.isChecked():
            target_floor, target_ceiling = self._suggest_y_range(counts)
            if self.y_ceiling <= 1.0:
                self.y_floor, self.y_ceiling = target_floor, target_ceiling
            else:
                self.y_floor = 0.65 * self.y_floor + 0.35 * target_floor
                self.y_ceiling = 0.65 * self.y_ceiling + 0.35 * target_ceiling
            if self.y_ceiling - self.y_floor < 1.0:
                self.y_ceiling = self.y_floor + 1.0
            self._set_y_limit_controls(self.y_floor, self.y_ceiling)
        else:
            self.y_floor = float(self.y_min_control.value())
            self.y_ceiling = max(
                self.y_floor + 1.0, float(self.y_max_control.value())
            )
        self.plot.setYRange(self.y_floor, self.y_ceiling, padding=0)
        self.reference_note.setPos(
            355, self.y_floor + (self.y_ceiling - self.y_floor) * 0.92
        )
        self.peak_card.value.setText(f"{summary.peak_nm:.1f} nm")
        self.signal_card.value.setText(f"{summary.peak_counts:,.0f}")
        self.integral_card.value.setText(f"{summary.integrated_counts_nm / 1e6:.2f} M")
        self.frame_label.setText(f"Frame {frame.sequence} / {frame.source}")
        self.saturation_label.setText(
            f"Saturation {summary.saturated_pixels}/288"
        )

    def capture_dark(self) -> None:
        if self.current_frame is None:
            return
        self.dark_reference = self.current_frame.counts.copy()
        self._append_diagnostic(f"Dark reference captured from frame {self.current_frame.sequence}.")

    def clear_dark(self) -> None:
        self.dark_reference = None
        self._append_diagnostic("Dark reference cleared.")

    def toggle_recording(self, active: bool) -> None:
        if active:
            stamp = time.strftime("%Y%m%d_%H%M%S")
            path = PROJECT_ROOT / "measurements" / f"c12880_{stamp}.csv"
            self.recorder.start(path)
            self.record_button.setText("Stop recording")
            self._append_diagnostic(f"Full-rate recording: {path}")
        else:
            path = self.recorder.stop()
            self.record_button.setText("Record raw CSV")
            if path:
                self._append_diagnostic(f"Recording closed: {path}")

    def export_plot(self) -> None:
        stamp = time.strftime("%Y%m%d_%H%M%S")
        default = PROJECT_ROOT / "measurements" / f"spectrum_{stamp}.png"
        default.parent.mkdir(parents=True, exist_ok=True)
        path, _ = QtWidgets.QFileDialog.getSaveFileName(
            self, "Export spectrum plot", str(default), "PNG image (*.png)"
        )
        if not path:
            return
        exporter = pg.exporters.ImageExporter(self.plot.plotItem)
        exporter.parameters()["width"] = 1800
        exporter.export(path)
        self._append_diagnostic(f"Plot exported: {path}")

    def _append_diagnostic(self, message: str) -> None:
        timestamp = time.strftime("%H:%M:%S")
        self.diagnostics.appendPlainText(f"[{timestamp}] {message}")

    def closeEvent(self, event: QtGui.QCloseEvent) -> None:
        self.timer.stop()
        self.recorder.stop()
        if self.worker:
            self.worker.request_stop()
        if self.thread:
            self.thread.quit()
            self.thread.wait(2_500)
        event.accept()


def run_gui(*, requested_port: str | None = None, demo_only: bool = False) -> int:
    pg.setConfigOptions(antialias=False)
    app = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
    app.setApplicationName("AgInTi Spectrum Studio")
    app.setOrganizationName("LazyingArt")
    app.setStyle("Fusion")
    window = SpectrumWindow(requested_port=requested_port, demo_only=demo_only)
    window.show()
    return app.exec()
