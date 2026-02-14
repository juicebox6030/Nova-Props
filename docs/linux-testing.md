# Linux test version (simulator) + existing options

## Existing solutions

- **Wokwi**: quick MCU emulation, but less suited to full local app/testing workflow.
- **PlatformIO unit tests**: good for isolated logic tests, not full runtime UI flow.
- **Hardware-in-loop**: highest fidelity, requires hardware.

## Simulator in this repo

`simulator/sim_app.py` now mirrors firmware Web UI route structure as closely as possible:

- `/` root dashboard
- `/wifi` WiFi settings
- `/dmx` sACN settings (including global `sacnBufferMs` jitter-smoothing window)
- `/subdevices` subdevice CRUD/editor with type-specific settings (including stepper seek mode, forward/return direction, and shortest-path tiebreaker)

In addition, it has **simulator-only hardware verification tools**:

- `/subdevices/test?id=<index>` to run per-subdevice test actions
- `/subdevices/home?id=<index>` to run stepper Home/Zero action
- `/sim/dmx` to apply a DMX frame from JSON
- `/api/events` to inspect hardware-probe events
- `/api/events/clear` to reset event history

## Start

```bash
python3 simulator/sim_app.py
```

Optional bind controls:

```bash
SIM_HOST=0.0.0.0 SIM_PORT=8080 python3 simulator/sim_app.py
```

## Core test flow

1. Open `http://127.0.0.1:8080`
2. Configure app via `/wifi`, `/dmx`, `/subdevices` like firmware UI
3. On `/subdevices`, use simulator-only DMX form to send frame
4. Verify resulting hardware events in `/api/events`

## DMX slots JSON format

Keys are DMX addresses and values are 0..255:

```json
{"1": 255, "2": 0, "3": 127}
```

## Why this helps

- Keeps local UX/test flow aligned with firmware pages.
- Adds deterministic observability for trigger verification.
- Allows fast iteration before flashing hardware.

- Performance note: stepper interval timing is cached and DC/pixel output writes are state-buffered; when validating behavior, verify unchanged DMX frames do not spam output updates.
- ESP32 deployment note: `esp32-full-dualcore` enables `USE_ESP32_DUAL_CORE=1` to run sACN + subdevice runtime work on core 1 while loop-task services (web/OTA) remain on the default core.
