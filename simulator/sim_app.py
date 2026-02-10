#!/usr/bin/env python3
"""Linux simulation app for Nova-Props firmware behavior.

- Serves a local web UI for subdevice CRUD.
- Emulates DMX/sACN application logic via HTTP test endpoint.
- Captures hardware actions in an event probe so behavior can be verified.
"""

from __future__ import annotations

import json
import threading
import time
from dataclasses import dataclass, asdict, field
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parent
CONFIG_PATH = ROOT / "config.sim.json"


SUBDEVICE_TYPES = {
    0: "Stepper",
    1: "DC Motor",
    2: "Relay",
    3: "LED",
    4: "Pixel Strip",
}


@dataclass
class SacnMapping:
    universe: int = 1
    startAddr: int = 1


@dataclass
class StepperRuntimeConfig:
    in1: int = 16
    in2: int = 17
    in3: int = 18
    in4: int = 19
    stepsPerRev: int = 4096
    maxDegPerSec: float = 90.0
    limitsEnabled: bool = False
    minDeg: float = 0.0
    maxDeg: float = 360.0
    homeOffsetSteps: int = 0


@dataclass
class DcMotorRuntimeConfig:
    dirPin: int = 25
    pwmPin: int = 27
    pwmChannel: int = 0
    pwmHz: int = 500
    pwmBits: int = 8
    deadband: int = 900
    maxPwm: int = 255


@dataclass
class RelayRuntimeConfig:
    pin: int = 22
    activeHigh: bool = True


@dataclass
class LedRuntimeConfig:
    pin: int = 21
    activeHigh: bool = True


@dataclass
class PixelRuntimeConfig:
    pin: int = 26
    count: int = 30
    brightness: int = 50


@dataclass
class SubdeviceConfig:
    enabled: bool = True
    name: str = "subdevice"
    type: int = 0
    map: SacnMapping = field(default_factory=SacnMapping)
    stepper: StepperRuntimeConfig = field(default_factory=StepperRuntimeConfig)
    dc: DcMotorRuntimeConfig = field(default_factory=DcMotorRuntimeConfig)
    relay: RelayRuntimeConfig = field(default_factory=RelayRuntimeConfig)
    led: LedRuntimeConfig = field(default_factory=LedRuntimeConfig)
    pixels: PixelRuntimeConfig = field(default_factory=PixelRuntimeConfig)


@dataclass
class AppConfig:
    sacnMode: int = 0
    lossMode: int = 0
    lossTimeoutMs: int = 1000
    subdevices: list[SubdeviceConfig] = field(default_factory=list)


class HardwareProbe:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._events: list[dict[str, Any]] = []

    def emit(self, event: str, payload: dict[str, Any]) -> None:
        with self._lock:
            self._events.append({
                "ts": time.time(),
                "event": event,
                "payload": payload,
            })

    def snapshot(self) -> list[dict[str, Any]]:
        with self._lock:
            return list(self._events)

    def clear(self) -> None:
        with self._lock:
            self._events.clear()


class SimApp:
    def __init__(self, config_path: Path):
        self.config_path = config_path
        self.cfg = AppConfig()
        self.probe = HardwareProbe()
        self._load()

    def _default_subdevices(self) -> list[SubdeviceConfig]:
        return [
            SubdeviceConfig(name="dc-1", type=1, map=SacnMapping(universe=1, startAddr=1)),
            SubdeviceConfig(name="stepper-1", type=0, map=SacnMapping(universe=1, startAddr=3)),
        ]

    def _load(self) -> None:
        if not self.config_path.exists():
            self.cfg.subdevices = self._default_subdevices()
            self.save()
            return

        raw = json.loads(self.config_path.read_text())
        self.cfg.sacnMode = int(raw.get("sacnMode", 0))
        self.cfg.lossMode = int(raw.get("lossMode", 0))
        self.cfg.lossTimeoutMs = int(raw.get("lossTimeoutMs", 1000))
        self.cfg.subdevices = []
        for item in raw.get("subdevices", []):
            self.cfg.subdevices.append(self._subdevice_from_dict(item))
        if not self.cfg.subdevices:
            self.cfg.subdevices = self._default_subdevices()

    def _subdevice_from_dict(self, d: dict[str, Any]) -> SubdeviceConfig:
        sd = SubdeviceConfig()
        sd.enabled = bool(d.get("enabled", True))
        sd.name = str(d.get("name", sd.name))
        sd.type = int(d.get("type", sd.type))
        m = d.get("map", {})
        sd.map = SacnMapping(universe=int(m.get("universe", 1)), startAddr=int(m.get("startAddr", 1)))
        sd.stepper = StepperRuntimeConfig(**{**asdict(sd.stepper), **d.get("stepper", {})})
        sd.dc = DcMotorRuntimeConfig(**{**asdict(sd.dc), **d.get("dc", {})})
        sd.relay = RelayRuntimeConfig(**{**asdict(sd.relay), **d.get("relay", {})})
        sd.led = LedRuntimeConfig(**{**asdict(sd.led), **d.get("led", {})})
        sd.pixels = PixelRuntimeConfig(**{**asdict(sd.pixels), **d.get("pixels", {})})
        return sd

    def save(self) -> None:
        payload = {
            "sacnMode": self.cfg.sacnMode,
            "lossMode": self.cfg.lossMode,
            "lossTimeoutMs": self.cfg.lossTimeoutMs,
            "subdevices": [self._subdevice_to_dict(sd) for sd in self.cfg.subdevices],
        }
        self.config_path.write_text(json.dumps(payload, indent=2))

    def _subdevice_to_dict(self, sd: SubdeviceConfig) -> dict[str, Any]:
        return {
            "enabled": sd.enabled,
            "name": sd.name,
            "type": sd.type,
            "map": asdict(sd.map),
            "stepper": asdict(sd.stepper),
            "dc": asdict(sd.dc),
            "relay": asdict(sd.relay),
            "led": asdict(sd.led),
            "pixels": asdict(sd.pixels),
        }

    def add_subdevice(self, dev_type: int, name: str) -> None:
        name = name.strip() or f"{SUBDEVICE_TYPES.get(dev_type, 'Device')}-{len(self.cfg.subdevices) + 1}"
        self.cfg.subdevices.append(SubdeviceConfig(name=name, type=dev_type))
        self.save()

    def delete_subdevice(self, idx: int) -> bool:
        if idx < 0 or idx >= len(self.cfg.subdevices):
            return False
        self.cfg.subdevices.pop(idx)
        self.save()
        return True

    def apply_dmx(self, universe: int, slots: dict[int, int]) -> None:
        for sd in self.cfg.subdevices:
            if not sd.enabled or sd.map.universe != universe:
                continue
            addr = sd.map.startAddr
            if sd.type == 1:  # DC
                raw = ((slots.get(addr, 0) << 8) | slots.get(addr + 1, 0))
                signed = raw - 32768
                if abs(signed) <= sd.dc.deadband:
                    signed = 0
                direction = "fwd" if signed > 0 else "rev" if signed < 0 else "stop"
                self.probe.emit("dc", {"name": sd.name, "value": signed, "dir": direction})
            elif sd.type == 0:  # stepper
                raw = ((slots.get(addr, 0) << 8) | slots.get(addr + 1, 0))
                deg = (raw / 65535.0) * 360.0
                self.probe.emit("stepper", {"name": sd.name, "target_deg": round(deg, 3)})
            elif sd.type == 2:  # relay
                on = slots.get(addr, 0) >= 128
                self.probe.emit("relay", {"name": sd.name, "on": on})
            elif sd.type == 3:  # led
                on = slots.get(addr, 0) >= 128
                self.probe.emit("led", {"name": sd.name, "on": on})
            elif sd.type == 4:  # pixels
                rgb = [slots.get(addr, 0), slots.get(addr + 1, 0), slots.get(addr + 2, 0)]
                self.probe.emit("pixels", {"name": sd.name, "rgb": rgb, "count": sd.pixels.count})


def html_page(app: SimApp) -> str:
    rows = []
    for i, sd in enumerate(app.cfg.subdevices):
        rows.append(
            f"<tr><td>{i}</td><td>{sd.name}</td><td>{SUBDEVICE_TYPES.get(sd.type,'Unknown')}</td>"
            f"<td>{'yes' if sd.enabled else 'no'}</td><td>{sd.map.universe}</td><td>{sd.map.startAddr}</td>"
            f"<td><a href='/delete?id={i}'>delete</a></td></tr>"
        )
    table = "".join(rows) or "<tr><td colspan='7'><i>No subdevices</i></td></tr>"
    return f"""
<!doctype html><html><body style='font-family:sans-serif;max-width:1100px;margin:16px'>
<h2>Nova Props Linux Simulator</h2>
<p>This sim mirrors subdevice config + DMX mapping behavior for local verification.</p>
<h3>Add subdevice</h3>
<form method='post' action='/add'>
  Name <input name='name'>
  Type <select name='type'>
    <option value='0'>Stepper</option><option value='1'>DC Motor</option>
    <option value='2'>Relay</option><option value='3'>LED</option><option value='4'>Pixel Strip</option>
  </select>
  <button type='submit'>Add Subdevice</button>
</form>
<h3>Subdevices</h3>
<table border='1' cellpadding='6'><tr><th>#</th><th>Name</th><th>Type</th><th>Enabled</th><th>Universe</th><th>Start</th><th>Actions</th></tr>{table}</table>

<h3>Simulate DMX Frame</h3>
<form method='post' action='/simulate'>
  Universe <input name='universe' value='1' size='4'>
  Slots JSON <input name='slots' size='80' value='{{"1":255,"2":0}}'>
  <button type='submit'>Apply DMX</button>
</form>
<p>Slots JSON format: key=DMX address, value=0..255.</p>

<p><a href='/api/subdevices'>API: subdevices</a> | <a href='/api/events'>API: events</a> | <a href='/api/events/clear'>clear events</a></p>
</body></html>
"""


class Handler(BaseHTTPRequestHandler):
    app: SimApp

    def _send(self, status: int, body: str, ctype: str = "text/html") -> None:
        data = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _redirect(self, location: str) -> None:
        self.send_response(HTTPStatus.SEE_OTHER)
        self.send_header("Location", location)
        self.end_headers()

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path == "/":
            return self._send(200, html_page(self.app))
        if parsed.path == "/api/subdevices":
            out = [self.app._subdevice_to_dict(sd) for sd in self.app.cfg.subdevices]
            return self._send(200, json.dumps(out, indent=2), "application/json")
        if parsed.path == "/api/events":
            return self._send(200, json.dumps(self.app.probe.snapshot(), indent=2), "application/json")
        if parsed.path == "/api/events/clear":
            self.app.probe.clear()
            return self._redirect("/")
        if parsed.path == "/delete":
            q = parse_qs(parsed.query)
            idx = int(q.get("id", ["-1"])[0])
            self.app.delete_subdevice(idx)
            return self._redirect("/")
        self._send(404, "not found", "text/plain")

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode()
        data = parse_qs(body)

        if parsed.path == "/add":
            name = data.get("name", [""])[0]
            dev_type = int(data.get("type", ["0"])[0])
            self.app.add_subdevice(dev_type, name)
            return self._redirect("/")

        if parsed.path == "/simulate":
            universe = int(data.get("universe", ["1"])[0])
            try:
                slots_raw = json.loads(data.get("slots", ["{}"])[0])
                slots = {int(k): max(0, min(255, int(v))) for k, v in slots_raw.items()}
            except Exception:
                return self._send(400, "Invalid slots JSON", "text/plain")
            self.app.apply_dmx(universe, slots)
            return self._redirect("/")

        self._send(404, "not found", "text/plain")


def main() -> None:
    app = SimApp(CONFIG_PATH)
    Handler.app = app
    server = ThreadingHTTPServer(("127.0.0.1", 8080), Handler)
    print("Simulator running at http://127.0.0.1:8080")
    server.serve_forever()


if __name__ == "__main__":
    main()
