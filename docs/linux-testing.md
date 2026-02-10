# Linux test version (simulator) + existing options

## Existing solutions

- **Wokwi**: quick MCU emulation, but less suited to full local app/testing workflow.
- **PlatformIO unit tests**: good for isolated logic tests, not full runtime UI flow.
- **Hardware-in-loop**: highest fidelity, requires hardware.

## Simulator in this repo

`simulator/sim_app.py` now mirrors firmware Web UI route structure as closely as possible:

- `/` root dashboard
- `/wifi` WiFi settings
- `/dmx` sACN settings
- `/subdevices` subdevice CRUD/editor

In addition, it has **simulator-only hardware verification tools**:

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
