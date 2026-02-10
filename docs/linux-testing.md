# Linux test version (simulator) + existing options

You asked whether an existing solution exists or if we needed to build one.

## Existing solutions

- **Wokwi** (browser simulation for ESP-class boards): great for quick emulation, but not ideal for full local Linux CI-style app flow.
- **PlatformIO unit tests**: useful for logic tests, but not a full runtime app with web UX + runtime subdevice control.
- **Hardware-in-loop** on Linux with real board connected: best fidelity, but still requires hardware.

## What was built here

A **Linux simulator app** in this repo: `simulator/sim_app.py`.

It provides:
- Web UI at `http://127.0.0.1:8080`
- "Add Subdevice" flow (Stepper/DC/Relay/LED/Pixels)
- Subdevice list + delete actions
- DMX frame simulation endpoint/form
- Hardware probe events so you can verify expected triggering behavior

### Start it

```bash
python3 simulator/sim_app.py
```

### Core test flow

1. Open `http://127.0.0.1:8080`
2. Add subdevices
3. Send a DMX test frame in the form (`Slots JSON`)
4. Inspect emitted hardware events:
   - Browser: `http://127.0.0.1:8080/api/events`
   - Clear: `http://127.0.0.1:8080/api/events/clear`

### DMX slots JSON format

Use object keys as DMX addresses:

```json
{"1": 255, "2": 0, "3": 127}
```

## Why this helps

- Lets you validate mapping + trigger behavior on Linux with no board connected.
- Gives a deterministic event stream (`/api/events`) for manual QA or automated tests.
- Keeps firmware code focused on embedded runtime while enabling desktop verification during development.
