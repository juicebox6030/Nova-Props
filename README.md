# Nova-Props

Nova-Props is a modular motion/props controller firmware + simulator stack designed for:

- **ESP32/ESP8266 firmware builds** (PlatformIO)
- **runtime-configured subdevices** (Stepper, DC motor, Relay, LED, Pixel strip)
- **sACN/DMX mapping per subdevice** (universe + start address)
- **local Linux simulation** for fast iteration without hardware

---

## Core capabilities

- Add/manage subdevices from the web interface.
- Map each subdevice to its own sACN universe/address range.
- Enable/disable feature packages at compile time (Web UI, sACN, OTA, Pixels).
- Run a Linux-hosted simulation web app with event probes for verification.

---

## Repository layout

```text
include/
  core/
    config.h           # App/subdevice model
    features.h         # Compile-time feature flags
    subdevices.h       # Runtime subdevice engine API
    web_ui.h           # Core web UI API
  platform/
    compat/            # Platform compatibility headers (WiFi/HTTP server)
    *.h                # Platform-neutral wrapper headers
    esp32/             # Current embedded platform contracts

src/
  core/
    config.cpp
    subdevices.cpp
    web_ui.cpp
  platform/esp32/
    config_storage.cpp
    dmx_sacn.cpp
    platform_services.cpp
    wifi_ota.cpp
  main.cpp

simulator/
  sim_app.py           # Linux simulator web app + hardware probe

docs/
  linux-testing.md     # Linux simulator usage
  architecture.md      # Architecture overview
```

---

## Build firmware (PlatformIO)

> Requires PlatformIO installed on your system.

Available environments are in `platformio.ini`:

- `esp32-full` (all features)
- `esp32-full-dualcore` (all features + dual-core runtime split)
- `esp32-lite` (OTA + pixels disabled)
- `esp8266-lite` (OTA + pixels disabled)

Examples:

```bash
pio run -e esp32-full
pio run -e esp32-full-dualcore
pio run -e esp32-lite
pio run -e esp8266-lite
```

Flash/upload example:

```bash
pio run -e esp32-full -t upload
```

---

## Feature packages (compile-time)

Defined in `include/core/features.h` and controlled by `build_flags`:

- `USE_WEB_UI`
- `USE_SACN`
- `USE_OTA`
- `USE_PIXELS`

Set these per environment in `platformio.ini` to fit small targets.

---


## Stepper + DC sACN channel behavior

- **DC motor (1 or 2 channels):** default is 8-bit command mode (`CH1` only). Optional `command16Bit = true` uses `CH1+CH2` for 16-bit commands. In both modes, a zero command is treated as hard OFF; non-zero commands run normally (with configured deadband, PWM scaling, and optional ramp buffer smoothing in ms).
- **Stepper (2 or 3 channels):**
  - Driver profile currently uses `Generic` (descriptor-based, so additional drivers can be added cleanly).
  - 8-bit position mode (`position16Bit = false`):
    - `CH1` (start address): absolute position `0..255` mapped into one full revolution.
    - `CH2` (start address + 1): velocity override (`0` off, `1..127` fast → slow CW, `128..255` slow → fast CW).
  - 16-bit position mode (`position16Bit = true`):
    - `CH1+CH2` (start + 0/1): absolute position `0..65535` mapped into one full revolution.
    - `CH3` (start + 2): same velocity override mapping.
  - `seekClockwise` sets absolute seek direction (`true` = always CW, `false` = always CCW).
- Stepper supports optional **home/e-stop switch** (`enabled`, `pin`, `active low`) and a **Home/Zero** action in the web UI.
- On DMX loss/restore, stepper logical position is preserved (coils are de-energized but state is held) to avoid reconnect jumps.
- Runtime command handling buffers output state (DC/pixels), includes configurable DC ramp-buffer smoothing to reduce packet jitter effects, and caches stepper timing intervals to keep the single-core loop responsive under high sACN packet rates.
- Global sACN ingest buffering (`sacnBufferMs`) can be set in `/dmx` (0..10000 ms). `0` keeps immediate packet apply behavior; non-zero values apply the latest frame per universe on a rate-limited window to smooth unstable links.
- Optional ESP32 dual-core mode (`USE_ESP32_DUAL_CORE=1`) moves the sACN + subdevice runtime loop onto core 1 while the default Arduino loop handles web/OTA services.

---

## Run Linux simulator

```bash
python3 simulator/sim_app.py
```

Then open:

- `http://127.0.0.1:8080` (UI)
- `http://127.0.0.1:8080/api/subdevices`
- `http://127.0.0.1:8080/api/events`

See full guide in `docs/linux-testing.md`.

---

## Documentation index

- `docs/architecture.md` — system architecture and runtime flow.
- `docs/linux-testing.md` — simulator and verification workflow.
- `AGENTS.md` — repository conventions/instructions for automated contributors.

---

## Status

This repo is currently structured around `platform/esp32` implementations with platform-neutral seams in place. Extending to more platforms should happen by adding new platform implementations behind existing `include/platform/*.h` wrappers.
