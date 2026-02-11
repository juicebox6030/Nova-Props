#!/usr/bin/env python3
"""Linux simulation app for Nova-Props firmware behavior.

Goal: keep Web UI flow as close as possible to firmware pages (`/`, `/wifi`, `/dmx`, `/subdevices`)
while adding simulator-only hardware probe/testing helpers.
"""

from __future__ import annotations

import json
import os
import threading
import time
from dataclasses import asdict, dataclass, field
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

MAX_SUBDEVICES = 12


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
    rampBufferMs: int = 120
    command16Bit: bool = False


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
    ssid: str = ""
    password: str = ""
    useStatic: bool = False
    ip: str = "192.168.1.60"
    gw: str = "192.168.1.1"
    mask: str = "255.255.255.0"

    sacnMode: int = 0
    sacnBufferMs: int = 0
    lossMode: int = 0
    lossTimeoutMs: int = 1000

    subdevices: list[SubdeviceConfig] = field(default_factory=list)


class HardwareProbe:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._events: list[dict[str, Any]] = []

    def emit(self, event: str, payload: dict[str, Any]) -> None:
        with self._lock:
            self._events.append({"ts": time.time(), "event": event, "payload": payload})

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
        self.packet_count = 0
        self.last_universe = 0
        self.dmx_active = False
        self._load()

    def _default_subdevices(self) -> list[SubdeviceConfig]:
        return [
            SubdeviceConfig(name="dc-1", type=1, map=SacnMapping(universe=1, startAddr=1)),
            SubdeviceConfig(name="stepper-1", type=0, map=SacnMapping(universe=1, startAddr=2)),
        ]

    def _load(self) -> None:
        if not self.config_path.exists():
            self.cfg.subdevices = self._default_subdevices()
            self.save()
            return

        raw = json.loads(self.config_path.read_text())
        self.cfg.ssid = str(raw.get("ssid", ""))
        self.cfg.password = str(raw.get("password", ""))
        self.cfg.useStatic = bool(raw.get("useStatic", False))
        self.cfg.ip = str(raw.get("ip", "192.168.1.60"))
        self.cfg.gw = str(raw.get("gw", "192.168.1.1"))
        self.cfg.mask = str(raw.get("mask", "255.255.255.0"))
        self.cfg.sacnMode = int(raw.get("sacnMode", 0))
        self.cfg.sacnBufferMs = int(raw.get("sacnBufferMs", 0))
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
            "ssid": self.cfg.ssid,
            "password": self.cfg.password,
            "useStatic": self.cfg.useStatic,
            "ip": self.cfg.ip,
            "gw": self.cfg.gw,
            "mask": self.cfg.mask,
            "sacnMode": self.cfg.sacnMode,
            "sacnBufferMs": self.cfg.sacnBufferMs,
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

    def add_subdevice(self, dev_type: int, name: str) -> tuple[bool, str]:
        if len(self.cfg.subdevices) >= MAX_SUBDEVICES:
            return False, "Max subdevices reached"
        default_name = f"{SUBDEVICE_TYPES.get(dev_type, 'Device')}-{len(self.cfg.subdevices) + 1}"
        self.cfg.subdevices.append(SubdeviceConfig(name=name.strip() or default_name, type=dev_type))
        self.save()
        return True, "Added"

    def delete_subdevice(self, idx: int) -> bool:
        if idx < 0 or idx >= len(self.cfg.subdevices):
            return False
        self.cfg.subdevices.pop(idx)
        self.save()
        return True


    def run_subdevice_test(self, idx: int) -> tuple[bool, str]:
        if idx < 0 or idx >= len(self.cfg.subdevices):
            return False, "Invalid subdevice id"
        sd = self.cfg.subdevices[idx]

        if sd.type == 0:
            step = max(1, sd.stepper.stepsPerRev // 4)
            deg = (step / max(1, sd.stepper.stepsPerRev)) * 360.0
            self.probe.emit("stepper_test", {"name": sd.name, "delta_deg": round(deg, 3)})
            return True, "Stepper test triggered"
        if sd.type == 1:
            self.probe.emit("dc_test", {"name": sd.name, "duty": max(0, min(255, sd.dc.maxPwm // 2)), "direction": 1})
            return True, "DC motor test triggered"
        if sd.type == 2:
            self.probe.emit("relay_test", {"name": sd.name, "pin": sd.relay.pin, "activeHigh": sd.relay.activeHigh})
            return True, "Relay test triggered"
        if sd.type == 3:
            self.probe.emit("led_test", {"name": sd.name, "pin": sd.led.pin, "activeHigh": sd.led.activeHigh})
            return True, "LED test triggered"
        if sd.type == 4:
            self.probe.emit("pixels_test", {"name": sd.name, "pin": sd.pixels.pin, "count": sd.pixels.count, "brightness": sd.pixels.brightness})
            return True, "Pixels test triggered"
        return False, "Unsupported subdevice type"

    def apply_dmx(self, universe: int, slots: dict[int, int]) -> None:
        self.packet_count += 1
        self.last_universe = universe
        self.dmx_active = True

        for sd in self.cfg.subdevices:
            if not sd.enabled or sd.map.universe != universe:
                continue
            addr = sd.map.startAddr
            if sd.type == 1:  # DC
                raw = ((slots.get(addr, 0) << 8) | slots.get(addr + 1, 0)) if sd.dc.command16Bit else (slots.get(addr, 0) * 257)
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


def esc(s: str) -> str:
    return (
        s.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def html_head(title: str) -> str:
    return (
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        f"<title>{esc(title)}</title></head>"
        "<body style='font-family:sans-serif;max-width:980px;margin:16px;'>"
    )


def type_options(selected: int) -> str:
    out = []
    for t in range(0, 5):
        sel = " selected" if t == selected else ""
        out.append(f"<option value='{t}'{sel}>{SUBDEVICE_TYPES[t]}</option>")
    return "".join(out)


def page_root(app: SimApp) -> str:
    s = html_head("Nova-Props Simulator")
    s += "<h2>Nova-Props Simulator</h2>"
    s += "<p><b>Mode:</b> STA + AP | <b>STA IP:</b> 127.0.0.1 | <b>AP IP:</b> 127.0.0.1</p>"
    s += (
        f"<p><b>Packets:</b> {app.packet_count} | <b>Last Universe:</b> {app.last_universe} "
        f"| <b>DMX Active:</b> {'yes' if app.dmx_active else 'no'} | <b>sACN buffer:</b> {app.cfg.sacnBufferMs} ms</p>"
    )
    s += "<p><a href='/wifi'>WiFi</a> | <a href='/dmx'>sACN</a> | <a href='/subdevices'>Subdevices</a></p>"

    s += f"<h3>Configured Subdevices ({len(app.cfg.subdevices)}/{MAX_SUBDEVICES})</h3><ul>"
    for i, sd in enumerate(app.cfg.subdevices, start=1):
        s += (
            f"<li>#{i} <b>{esc(sd.name)}</b> [{SUBDEVICE_TYPES.get(sd.type,'Unknown')}] "
            f"U{sd.map.universe} @ {sd.map.startAddr} {'(enabled)' if sd.enabled else '(disabled)'}</li>"
        )
    s += "</ul></body></html>"
    return s


def page_wifi(app: SimApp) -> str:
    s = html_head("WiFi")
    s += "<h2>WiFi Settings</h2><form method='POST' action='/savewifi'>"
    s += f"SSID: <input name='ssid' value='{esc(app.cfg.ssid)}'><br><br>"
    s += f"Password: <input name='pass' type='password' value='{esc(app.cfg.password)}'><br><br>"
    checked = "checked" if app.cfg.useStatic else ""
    s += f"<label><input name='st' type='checkbox' {checked}> Static IP</label><br><br>"
    s += f"IP: <input name='ip' value='{esc(app.cfg.ip)}'><br>"
    s += f"GW: <input name='gw' value='{esc(app.cfg.gw)}'><br>"
    s += f"Mask: <input name='mask' value='{esc(app.cfg.mask)}'><br><br>"
    s += "<button type='submit'>Save & Reboot (simulated)</button></form><p><a href='/'>Back</a></p></body></html>"
    return s


def page_dmx(app: SimApp) -> str:
    s = html_head("sACN")
    s += "<h2>sACN Settings</h2><form method='POST' action='/savedmx'>"
    s += "Mode: <select name='m'>"
    s += f"<option value='0'{' selected' if app.cfg.sacnMode == 0 else ''}>Unicast</option>"
    s += f"<option value='1'{' selected' if app.cfg.sacnMode == 1 else ''}>Multicast</option>"
    s += "</select><br><br>"
    s += f"sACN buffer (ms): <input name='sb' type='number' min='0' max='10000' value='{app.cfg.sacnBufferMs}'><br><br>"
    s += f"DMX loss timeout (ms): <input name='to' type='number' min='100' max='60000' value='{app.cfg.lossTimeoutMs}'><br><br>"
    s += "On loss: <select name='lm'>"
    s += f"<option value='0'{' selected' if app.cfg.lossMode == 0 else ''}>Force OFF</option>"
    s += f"<option value='2'{' selected' if app.cfg.lossMode == 2 else ''}>Hold Last</option>"
    s += "</select><br><br>"
    s += "<button type='submit'>Save</button></form><p><a href='/'>Back</a></p></body></html>"
    return s


def render_type_specific_fields(sd: SubdeviceConfig) -> str:
    stlim = "checked" if sd.stepper.limitsEnabled else ""
    rlah = "checked" if sd.relay.activeHigh else ""
    ledah = "checked" if sd.led.activeHigh else ""

    if sd.type == 0:
        return (
            "<fieldset><legend>Stepper</legend>"
            f"IN1 <input name='st1' type='number' value='{sd.stepper.in1}'> "
            f"IN2 <input name='st2' type='number' value='{sd.stepper.in2}'> "
            f"IN3 <input name='st3' type='number' value='{sd.stepper.in3}'> "
            f"IN4 <input name='st4' type='number' value='{sd.stepper.in4}'><br><br>"
            f"Steps/rev <input name='stspr' type='number' value='{sd.stepper.stepsPerRev}'> "
            f"Max deg/sec <input name='stspd' type='number' step='0.1' value='{sd.stepper.maxDegPerSec}'><br><br>"
            f"<label><input type='checkbox' name='stlim' {stlim}>Limits</label> "
            f"Min <input name='stmin' type='number' step='0.1' value='{sd.stepper.minDeg}'> "
            f"Max <input name='stmax' type='number' step='0.1' value='{sd.stepper.maxDeg}'></fieldset><br>"
        )
    if sd.type == 1:
        return (
            "<fieldset><legend>DC Motor</legend>"
            f"DIR <input name='dcdir' type='number' value='{sd.dc.dirPin}'> "
            f"PWM <input name='dcpwm' type='number' value='{sd.dc.pwmPin}'> "
            f"CH <input name='dcch' type='number' value='{sd.dc.pwmChannel}'><br><br>"
            f"Hz <input name='dchz' type='number' value='{sd.dc.pwmHz}'> "
            f"Bits <input name='dcbits' type='number' min='1' max='8' value='{sd.dc.pwmBits}'> "
            f"Deadband <input name='dcdb' type='number' value='{sd.dc.deadband}'> "
            f"MaxPWM <input name='dcmx' type='number' value='{sd.dc.maxPwm}'><br><br>"
            f"<label><input type='checkbox' name='dc16' {'checked' if sd.dc.command16Bit else ''}>16-bit sACN command (CH1+CH2)</label><br>"
            f"Ramp buffer ms <input name='dcramp' type='number' min='0' max='10000' value='{sd.dc.rampBufferMs}'><br>"
            "<small>8-bit mode: CH1 command (default). 16-bit mode: CH1+CH2 command.</small></fieldset><br>"
        )
    if sd.type == 2:
        return (
            "<fieldset><legend>Relay</legend>"
            f"Relay pin <input name='rlpin' type='number' value='{sd.relay.pin}'> "
            f"Relay active high <input type='checkbox' name='rlah' {rlah}></fieldset><br>"
        )
    if sd.type == 3:
        return (
            "<fieldset><legend>LED</legend>"
            f"LED pin <input name='ledpin' type='number' value='{sd.led.pin}'> "
            f"LED active high <input type='checkbox' name='ledah' {ledah}></fieldset><br>"
        )
    if sd.type == 4:
        return (
            "<fieldset><legend>Pixel Strip</legend>"
            f"Pixel pin <input name='pxpin' type='number' value='{sd.pixels.pin}'> "
            f"Count <input name='pxcount' type='number' value='{sd.pixels.count}'> "
            f"Brightness <input name='pxb' type='number' value='{sd.pixels.brightness}'></fieldset><br>"
        )
    return ""


def render_subdevice_form(i: int, sd: SubdeviceConfig) -> str:
    en = "checked" if sd.enabled else ""

    s = ""
    s += f"<details style='border:1px solid #ccc;padding:8px;margin:10px 0;' open><summary><b>#{i+1} {esc(sd.name)}</b> ({SUBDEVICE_TYPES.get(sd.type,'Unknown')})</summary>"
    s += "<form method='POST' action='/subdevices/update'>"
    s += f"<input type='hidden' name='id' value='{i}'>"
    s += f"Name: <input name='name' value='{esc(sd.name)}'> &nbsp;"
    s += f"Enabled: <input type='checkbox' name='en' {en}><br><br>"
    s += f"Type: <select name='type'>{type_options(sd.type)}</select><br><br>"
    s += f"Universe: <input name='u' type='number' min='1' max='63999' value='{sd.map.universe}'> &nbsp;"
    s += f"Start addr: <input name='a' type='number' min='1' max='512' value='{sd.map.startAddr}'><br><br>"

    s += render_type_specific_fields(sd)

    s += "<button type='submit'>Save Subdevice</button> "
    s += f"<a href='/subdevices/test?id={i}'>Run Test</a> | "
    s += f"<a href='/subdevices/delete?id={i}' onclick=\"return confirm('Delete subdevice?');\">Delete</a>"
    s += "</form></details>"
    return s


def page_subdevices(app: SimApp) -> str:
    s = html_head("Subdevices")
    s += "<h2>Subdevices</h2>"
    s += "<p>Add hardware blocks and map each to Universe/Address for sACN.</p>"

    s += "<form method='POST' action='/subdevices/add' style='padding:8px;border:1px solid #ccc;'>"
    s += "Name <input name='name' placeholder='optional'> "
    s += f"Type <select name='type'>{type_options(0)}</select> "
    s += "<button type='submit'>Add Subdevice</button></form>"

    for i, sd in enumerate(app.cfg.subdevices):
        s += render_subdevice_form(i, sd)

    s += "<hr><h3>Simulator-only Hardware Probe</h3>"
    s += "<form method='POST' action='/sim/dmx'>Universe <input name='universe' value='1' size='4'> "
    s += "Slots JSON <input name='slots' size='80' value='{""1"":255,""2"":0}'> "
    s += "<button type='submit'>Apply DMX Frame</button></form>"
    s += "<p><a href='/api/events'>View probe events</a> | <a href='/api/events/clear'>Clear probe events</a></p>"
    s += "<p><a href='/'>Back</a></p></body></html>"
    return s


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
            return self._send(200, page_root(self.app))
        if parsed.path == "/wifi":
            return self._send(200, page_wifi(self.app))
        if parsed.path == "/dmx":
            return self._send(200, page_dmx(self.app))
        if parsed.path == "/subdevices":
            return self._send(200, page_subdevices(self.app))
        if parsed.path == "/api/subdevices":
            out = [self.app._subdevice_to_dict(sd) for sd in self.app.cfg.subdevices]
            return self._send(200, json.dumps(out, indent=2), "application/json")
        if parsed.path == "/api/events":
            return self._send(200, json.dumps(self.app.probe.snapshot(), indent=2), "application/json")
        if parsed.path == "/api/events/clear":
            self.app.probe.clear()
            return self._redirect("/subdevices")
        if parsed.path == "/subdevices/delete":
            q = parse_qs(parsed.query)
            idx = int(q.get("id", ["-1"])[0])
            self.app.delete_subdevice(idx)
            return self._redirect("/subdevices")
        if parsed.path == "/subdevices/test":
            q = parse_qs(parsed.query)
            idx = int(q.get("id", ["-1"])[0])
            ok, msg = self.app.run_subdevice_test(idx)
            if not ok:
                return self._send(400, msg, "text/plain")
            return self._redirect("/subdevices")
        self._send(404, "not found", "text/plain")

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode()
        data = parse_qs(body)

        if parsed.path == "/savewifi":
            self.app.cfg.ssid = data.get("ssid", [""])[0]
            self.app.cfg.password = data.get("pass", [""])[0]
            self.app.cfg.useStatic = "st" in data
            self.app.cfg.ip = data.get("ip", [self.app.cfg.ip])[0]
            self.app.cfg.gw = data.get("gw", [self.app.cfg.gw])[0]
            self.app.cfg.mask = data.get("mask", [self.app.cfg.mask])[0]
            self.app.save()
            return self._redirect("/wifi")

        if parsed.path == "/savedmx":
            self.app.cfg.sacnMode = int(data.get("m", [str(self.app.cfg.sacnMode)])[0])
            self.app.cfg.sacnBufferMs = max(0, min(10000, int(data.get("sb", [str(self.app.cfg.sacnBufferMs)])[0])))
            self.app.cfg.lossTimeoutMs = int(data.get("to", [str(self.app.cfg.lossTimeoutMs)])[0])
            self.app.cfg.lossMode = int(data.get("lm", [str(self.app.cfg.lossMode)])[0])
            self.app.save()
            return self._redirect("/dmx")

        if parsed.path == "/subdevices/add":
            name = data.get("name", [""])[0]
            dev_type = int(data.get("type", ["0"])[0])
            ok, msg = self.app.add_subdevice(dev_type, name)
            if not ok:
                return self._send(400, msg, "text/plain")
            return self._redirect("/subdevices")

        if parsed.path == "/subdevices/update":
            idx = int(data.get("id", ["-1"])[0])
            if idx < 0 or idx >= len(self.app.cfg.subdevices):
                return self._send(400, "Invalid id", "text/plain")
            sd = self.app.cfg.subdevices[idx]
            sd.enabled = "en" in data
            sd.type = int(data.get("type", [str(sd.type)])[0])
            sd.name = data.get("name", [sd.name])[0] or f"subdevice-{idx+1}"
            sd.map.universe = int(data.get("u", [str(sd.map.universe)])[0])
            sd.map.startAddr = int(data.get("a", [str(sd.map.startAddr)])[0])

            if sd.type == 0:
                sd.stepper.in1 = int(data.get("st1", [str(sd.stepper.in1)])[0])
                sd.stepper.in2 = int(data.get("st2", [str(sd.stepper.in2)])[0])
                sd.stepper.in3 = int(data.get("st3", [str(sd.stepper.in3)])[0])
                sd.stepper.in4 = int(data.get("st4", [str(sd.stepper.in4)])[0])
                sd.stepper.stepsPerRev = int(data.get("stspr", [str(sd.stepper.stepsPerRev)])[0])
                sd.stepper.maxDegPerSec = float(data.get("stspd", [str(sd.stepper.maxDegPerSec)])[0])
                sd.stepper.limitsEnabled = "stlim" in data
                sd.stepper.minDeg = float(data.get("stmin", [str(sd.stepper.minDeg)])[0])
                sd.stepper.maxDeg = float(data.get("stmax", [str(sd.stepper.maxDeg)])[0])
            elif sd.type == 1:
                sd.dc.dirPin = int(data.get("dcdir", [str(sd.dc.dirPin)])[0])
                sd.dc.pwmPin = int(data.get("dcpwm", [str(sd.dc.pwmPin)])[0])
                sd.dc.pwmChannel = int(data.get("dcch", [str(sd.dc.pwmChannel)])[0])
                sd.dc.pwmHz = int(data.get("dchz", [str(sd.dc.pwmHz)])[0])
                sd.dc.pwmBits = max(1, min(8, int(data.get("dcbits", [str(sd.dc.pwmBits)])[0])))
                sd.dc.command16Bit = "dc16" in data
                sd.dc.deadband = int(data.get("dcdb", [str(sd.dc.deadband)])[0])
                sd.dc.maxPwm = int(data.get("dcmx", [str(sd.dc.maxPwm)])[0])
                sd.dc.rampBufferMs = max(0, min(10000, int(data.get("dcramp", [str(sd.dc.rampBufferMs)])[0])))
            elif sd.type == 2:
                sd.relay.pin = int(data.get("rlpin", [str(sd.relay.pin)])[0])
                sd.relay.activeHigh = "rlah" in data
            elif sd.type == 3:
                sd.led.pin = int(data.get("ledpin", [str(sd.led.pin)])[0])
                sd.led.activeHigh = "ledah" in data
            elif sd.type == 4:
                sd.pixels.pin = int(data.get("pxpin", [str(sd.pixels.pin)])[0])
                sd.pixels.count = int(data.get("pxcount", [str(sd.pixels.count)])[0])
                sd.pixels.brightness = int(data.get("pxb", [str(sd.pixels.brightness)])[0])

            self.app.save()
            return self._redirect("/subdevices")

        if parsed.path == "/sim/dmx":
            universe = int(data.get("universe", ["1"])[0])
            try:
                slots_raw = json.loads(data.get("slots", ["{}"])[0])
                slots = {int(k): max(0, min(255, int(v))) for k, v in slots_raw.items()}
            except Exception:
                return self._send(400, "Invalid slots JSON", "text/plain")
            self.app.apply_dmx(universe, slots)
            return self._redirect("/subdevices")

        self._send(404, "not found", "text/plain")


def main() -> None:
    app = SimApp(CONFIG_PATH)
    Handler.app = app
    host = os.environ.get("SIM_HOST", "127.0.0.1")
    port = int(os.environ.get("SIM_PORT", "8080"))
    server = ThreadingHTTPServer((host, port), Handler)
    print(f"Simulator running at http://{host}:{port}")
    server.serve_forever()


if __name__ == "__main__":
    main()
