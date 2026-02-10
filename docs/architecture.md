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
6. Main loop executes service handlers and subdevice ticks

## Subdevice model

Each subdevice includes:

- `type` (Stepper/DC/Relay/LED/Pixels)
- `enabled`
- `name`
- `map` (`universe`, `startAddr`)
- Type-specific settings

A single config can contain multiple subdevices.

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
