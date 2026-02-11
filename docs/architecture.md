# Architecture

## Overview

Nova-Props is split into three layers:

1. **Core domain** (`include/core`, `src/core`)
   - Subdevice model and runtime behavior
   - Feature flags
   - Core web UI logic
2. **Platform abstraction** (`include/platform`)
   - Compatibility types (HTTP server, WiFi)
   - Platform-neutral wrapper headers
3. **Platform implementation** (`src/platform/esp32`)
   - Config persistence
   - sACN ingestion
   - WiFi/OTA integration
   - Platform services

`src/main.cpp` is orchestration glue.

## Runtime flow

1. Boot + filesystem init
2. Load config + sanity checks
3. Initialize subdevices from config
4. Start networking
5. Start optional services (Web UI, OTA, sACN)
   - sACN starts in either AP-only fallback mode or STA-connected mode
6. Main loop executes service handlers and subdevice ticks

## Subdevice model

Each subdevice includes:

- `type` (Stepper/DC/Relay/LED/Pixels)
- `enabled`
- `name`
- `map` (`universe`, `startAddr`)
- Type-specific settings

A single config can contain multiple subdevices.

### Stepper runtime notes

- DC subdevices consume 1 slot by default (`CH1` 8-bit signed-style command) or 2 slots when `command16Bit` is enabled (`CH1+CH2`).
- Stepper subdevices consume 2 or 3 DMX channels per mapping:
  - 8-bit mode: `CH1` absolute position, `CH2` velocity override.
  - 16-bit mode: `CH1+CH2` absolute position, `CH3` velocity override.
- Stepper runtime stores internal step position and target state continuously between packets; on DMX loss it now de-energizes coils but preserves logical position/target state so reconnect does not introduce synthetic catch-up motion.
- Absolute seek direction is configurable (`seekClockwise`) so position seeks can be forced CW or CCW.
- Subdevice runtime configs now include driver enums (`Generic` currently) for Stepper/DC/Pixels to support descriptor-based driver expansion.
- Runtime output writes are now state-buffered for DC/Pixels, DC outputs can optionally ramp over a configurable buffer window (ms), and stepper timing intervals are cached per command to reduce per-tick CPU load on single-core MCUs.
- sACN ingest supports a global per-universe frame hold window (`sacnBufferMs`): `0` applies immediately, non-zero applies latest buffered frames on a rate-limited interval.
- Optional ESP32 dual-core scheduling (`USE_ESP32_DUAL_CORE`) pins the runtime worker (sACN ingest + subdevice tick + DMX loss enforcement) to core 1 while web/OTA stay in the default loop task.
- Optional per-stepper home/e-stop switch config can zero and stop the motor in runtime.


## Feature packaging

Compile-time flags are defined in `include/core/features.h` and set per PlatformIO environment:

- `USE_WEB_UI`
- `USE_SACN`
- `USE_OTA`
- `USE_PIXELS`

This allows smaller binaries for constrained targets.

## Linux simulator

`simulator/sim_app.py` provides a desktop feedback loop:

- Subdevice CRUD UI
- DMX simulation input
- Probe event output (`/api/events`) to verify intended hardware actions

Use this for rapid behavior checks before firmware flashing.
