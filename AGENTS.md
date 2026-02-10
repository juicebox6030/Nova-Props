# AGENTS.md

This file defines repository-level guidance for human and automated contributors.

## Scope

These instructions apply to the entire repository tree unless a deeper `AGENTS.md` overrides them.

## Project goals

1. Keep core logic portable.
2. Keep platform-specific logic isolated under `src/platform/<platform>` and `include/platform/<platform>`.
3. Preserve runtime configurability for subdevices and sACN mappings.
4. Keep build profiles small and explicit for constrained devices.

## Architecture conventions

- Prefer placing reusable logic in `src/core` + `include/core`.
- Prefer platform-neutral include wrappers in `include/platform/*.h`.
- Platform-specific includes should stay inside platform implementations when possible.
- New optional capabilities should be feature-gated via `include/core/features.h` and `platformio.ini` flags.

## Documentation expectations

When making meaningful behavior/architecture changes, update relevant docs:

- `README.md` for user-facing build/run instructions.
- `docs/architecture.md` for architecture changes.
- `docs/linux-testing.md` when simulator or test flow changes.

## Testing expectations

At minimum, run checks relevant to modified areas:

- Python files: `python3 -m py_compile ...`
- General patch hygiene: `git diff --check`
- Firmware builds where possible: `pio run -e <env>`

If environment limitations block checks, document exactly what was attempted and why it failed.

## Code style

- Keep functions focused and composable.
- Avoid hidden platform assumptions in core modules.
- Prefer explicit names over terse abbreviations for new APIs.
- Avoid introducing global mutable state unless required for embedded runtime performance.

## Pull request notes

PR descriptions should include:

- Motivation
- What changed
- Testing performed (+ limitations)
- Any migration steps for users
