# AGENTS.md

Guidance for coding agents (and contributors using them) working in this repo. End-user docs live
in [README.md](README.md) and [docs/](docs/); this file is about *working on the code*.

## What this is

An ESPHome external component (`esphome/components/danfoss_icon/`) that impersonates the Danfoss
Icon App Module on the controller's RJ45 RS-485 link and exposes the rooms to Home Assistant. See
the README for the full picture.

## Layout

- `esphome/components/danfoss_icon/` — the component (C++ runtime + Python codegen).
- `esphome/danfoss-icon.yaml` — the example / bring-up config. `secrets.yaml` is gitignored; copy
  `secrets.yaml.example`.
- `docs/` — public HARDWARE / PROTOCOL / INSTALL docs.
- `.clang-format` / `pyproject.toml` / `.yamllint` — root tooling configs mirroring upstream ESPHome
  (see Formatting & linting).
- `reverse-engineering/` — **gitignored, local-only.** Firmware dumps, Ghidra projects, captures,
  RE notes. Never published; never reference these files from tracked code or docs.

## Build & test

- Host protocol test (no hardware needed):
  `cd esphome/components/danfoss_icon && c++ -std=c++17 -o /tmp/dtest test_protocol.cpp && /tmp/dtest`
- Validate the example config: `cd esphome && esphome config danfoss-icon.yaml` (needs `secrets.yaml`).
- Full build: `esphome compile danfoss-icon.yaml`.

## Formatting & linting

Mirrors upstream ESPHome's toolchain — **run these before committing** (configs are at the repo
root; there is no CI to catch misses):

- **C++** — `clang-format -i esphome/components/danfoss_icon/*.h esphome/components/danfoss_icon/*.cpp`
  (uses `.clang-format`).
- **Python** — `ruff format . && ruff check .` (uses `pyproject.toml`).
- **YAML** — `yamllint .` (uses `.yamllint`).

If a tool isn't installed locally, run the pinned-or-latest version ephemerally, e.g.
`uvx ruff format .`, `uvx yamllint .`, `uvx --from clang-format clang-format -i …`. After touching
`protocol.h`, also re-run the host protocol test above.

## Architecture (start at `danfoss_icon.h`)

- `protocol.h` — pure C++, no ESPHome dependency, so it's host-testable: framing, CRC-16/MODBUS,
  frame scan, `build_read`/`build_write`, per-attribute value sizes, and the status/product-id
  name helpers.
- `DanfossIconHub` — owns the UART, a non-blocking single-outstanding-transaction engine, a tiered
  poll list, and a write queue. Slices each `0x0D` reply by attribute and dispatches to listeners.
- Entities (`climate`, `sensor`, `text_sensor`, `number`, `status`) implement `DanfossIconListener`
  (`on_attr`) and register which rooms/attributes they need polled.

## Conventions (keep consistent)

- **Terminology:** use **controller** everywhere (entities, YAML, docs, code). "master" appears
  only in the deliberate bridge notes (PROTOCOL.md and the `__init__.py` header) that map our term
  to Danfoss's "master controller" / "slave" naming. Don't reintroduce master/slave.
- **No RE references in published code/docs** — don't cite anything from the gitignored
  `reverse-engineering/` tree (notes, Ghidra symbols, capture/decoder scripts). Explain behaviour
  self-containedly.
- **ESPHome codegen:** declare child-entity IDs in the schema via
  `cv.GenerateID(...): cv.declare_id(...)`; core component imports are aliased (`sensor as sensor_`,
  …) because the same-named submodules shadow them.
- Otherwise, match the style of the surrounding code and docs.

## Don't

- Commit `secrets.yaml`, the `.esphome/` build cache, or anything under `reverse-engineering/`.
- Change the hardware-validated protocol/transaction logic without a clear reason — it works against
  real hardware. Prefer additive changes; run the host protocol test after touching `protocol.h`.
