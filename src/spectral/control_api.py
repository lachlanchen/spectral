"""Thread-safe HTTP control plane shared by the GUI and CLI client."""

from __future__ import annotations

from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from importlib import resources
import json
import queue
import threading
from typing import Any
from urllib import error, request
from urllib.parse import urlparse


@dataclass(slots=True)
class ControlRequest:
    action: str
    payload: dict[str, Any]
    reply: queue.Queue[dict[str, Any]]


class ControlBridge:
    """Move API requests onto the Qt thread without touching widgets remotely."""

    def __init__(self) -> None:
        self._commands: queue.Queue[ControlRequest] = queue.Queue()
        self._state_lock = threading.Lock()
        self._state: dict[str, Any] = {"state": "starting"}
        self._spectrum: dict[str, Any] = {
            "sequence": -1, "wavelengths_nm": [], "counts": []
        }

    def request(
        self, action: str, payload: dict[str, Any] | None = None, timeout: float = 3.0
    ) -> dict[str, Any]:
        reply: queue.Queue[dict[str, Any]] = queue.Queue(maxsize=1)
        self._commands.put(ControlRequest(action, payload or {}, reply))
        try:
            return reply.get(timeout=timeout)
        except queue.Empty as exc:
            raise TimeoutError(f"control action timed out: {action}") from exc

    def drain(self, limit: int = 32) -> list[ControlRequest]:
        commands: list[ControlRequest] = []
        for _ in range(limit):
            try:
                commands.append(self._commands.get_nowait())
            except queue.Empty:
                break
        return commands

    def update_state(self, state: dict[str, Any]) -> None:
        with self._state_lock:
            self._state = state.copy()

    def status(self) -> dict[str, Any]:
        with self._state_lock:
            return self._state.copy()

    def update_spectrum(self, spectrum: dict[str, Any]) -> None:
        with self._state_lock:
            self._spectrum = spectrum.copy()

    def spectrum(self) -> dict[str, Any]:
        with self._state_lock:
            return self._spectrum.copy()


class _ControlHttpServer(ThreadingHTTPServer):
    allow_reuse_address = True
    daemon_threads = True


class ControlApiServer:
    ROUTES = {
        "/api/v1/exposure": "set_exposure",
        "/api/v1/exposure/auto": "set_auto_exposure",
        "/api/v1/exposure/meter": "meter_exposure",
        "/api/v1/y-scale/auto": "set_y_auto",
        "/api/v1/y-scale/fit": "fit_y",
        "/api/v1/y-scale/limits": "set_y_limits",
        "/api/v1/smoothing": "set_smoothing",
        "/api/v1/acquisition": "set_acquisition",
        "/api/v1/dark/capture": "capture_dark",
        "/api/v1/dark/clear": "clear_dark",
        "/api/v1/recording": "set_recording",
    }

    def __init__(self, bridge: ControlBridge, host: str, port: int) -> None:
        self.bridge = bridge
        self.host = host
        self.port = int(port)
        self.httpd: _ControlHttpServer | None = None
        self.thread: threading.Thread | None = None

    def start(self) -> None:
        bridge = self.bridge
        routes = self.ROUTES
        web_root = resources.files("spectral").joinpath("web")
        icon = resources.files("spectral").joinpath("resources", "icon.svg")
        assets = {
            "/": ("text/html; charset=utf-8", web_root.joinpath("index.html").read_bytes()),
            "/index.html": ("text/html; charset=utf-8", web_root.joinpath("index.html").read_bytes()),
            "/styles.css": ("text/css; charset=utf-8", web_root.joinpath("styles.css").read_bytes()),
            "/app.js": ("text/javascript; charset=utf-8", web_root.joinpath("app.js").read_bytes()),
            "/icon.svg": ("image/svg+xml", icon.read_bytes()),
        }

        class Handler(BaseHTTPRequestHandler):
            server_version = "AgInTiSpectrumAPI/1.0"

            def _send(self, status: int, payload: dict[str, Any]) -> None:
                encoded = json.dumps(payload, ensure_ascii=False).encode("utf-8")
                self.send_response(status)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Content-Length", str(len(encoded)))
                self.send_header("Access-Control-Allow-Origin", "*")
                self.send_header("Access-Control-Allow-Headers", "Content-Type")
                self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
                self.end_headers()
                self.wfile.write(encoded)

            def _payload(self) -> dict[str, Any]:
                length = int(self.headers.get("Content-Length", "0"))
                if length <= 0:
                    return {}
                value = json.loads(self.rfile.read(length).decode("utf-8"))
                if not isinstance(value, dict):
                    raise ValueError("JSON request body must be an object")
                return value

            def _send_asset(self, content_type: str, content: bytes) -> None:
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(content)))
                self.send_header("Cache-Control", "no-cache")
                self.end_headers()
                self.wfile.write(content)

            def do_OPTIONS(self) -> None:  # noqa: N802
                self._send(HTTPStatus.NO_CONTENT, {})

            def do_GET(self) -> None:  # noqa: N802
                path = urlparse(self.path).path.rstrip("/") or "/"
                if path in ("/health", "/api/v1/status"):
                    self._send(HTTPStatus.OK, {"ok": True, "status": bridge.status()})
                elif path == "/api/v1/spectrum":
                    self._send(HTTPStatus.OK, {"ok": True, "spectrum": bridge.spectrum()})
                elif path in assets:
                    self._send_asset(*assets[path])
                else:
                    self._send(HTTPStatus.NOT_FOUND, {"ok": False, "error": "not found"})

            def do_POST(self) -> None:  # noqa: N802
                path = urlparse(self.path).path.rstrip("/")
                try:
                    payload = self._payload()
                    if path == "/api/v1/command":
                        action = str(payload.pop("action"))
                    else:
                        action = routes[path]
                    response = bridge.request(action, payload)
                    self._send(
                        HTTPStatus.OK if response.get("ok") else HTTPStatus.BAD_REQUEST,
                        response,
                    )
                except KeyError:
                    self._send(HTTPStatus.NOT_FOUND, {"ok": False, "error": "not found"})
                except Exception as exc:
                    self._send(
                        HTTPStatus.BAD_REQUEST,
                        {"ok": False, "error": f"{type(exc).__name__}: {exc}"},
                    )

            def log_message(self, _format: str, *_args: object) -> None:
                return

        self.httpd = _ControlHttpServer((self.host, self.port), Handler)
        self.thread = threading.Thread(
            target=self.httpd.serve_forever,
            kwargs={"poll_interval": 0.2},
            name="spectrum-control-api",
            daemon=True,
        )
        self.thread.start()

    def stop(self) -> None:
        if self.httpd is not None:
            self.httpd.shutdown()
            self.httpd.server_close()
            self.httpd = None
        if self.thread is not None:
            self.thread.join(timeout=1.0)
            self.thread = None


def api_request(
    base_url: str,
    path: str,
    payload: dict[str, Any] | None = None,
) -> dict[str, Any]:
    url = base_url.rstrip("/") + path
    data = None if payload is None else json.dumps(payload).encode("utf-8")
    method = "GET" if payload is None else "POST"
    req = request.Request(
        url,
        data=data,
        method=method,
        headers={"Content-Type": "application/json"},
    )
    try:
        with request.urlopen(req, timeout=5.0) as response:
            return json.loads(response.read().decode("utf-8"))
    except error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"API {exc.code}: {detail}") from exc
