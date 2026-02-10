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
- `esp32-lite` (OTA + pixels disabled)
- `esp8266-lite` (OTA + pixels disabled)

Examples:

```bash
pio run -e esp32-full
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
