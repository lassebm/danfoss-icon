# Wire protocol

The link between the Icon controller (088U1141) and the App Module (088U1101) over the RJ45 patch
cable. This is the **"Cumulus"** attribute protocol — the same attribute model the controller, the
original App Module, and the Danfoss cloud all share.

You don't need any of this to *use* the project — it's here for the curious and for contributors.
The implementation lives in [`esphome/components/danfoss_icon/protocol.h`](../esphome/components/danfoss_icon/protocol.h),
with a host-side unit test in `test_protocol.cpp`.

> **Why you'll see "master" elsewhere.** This doc — like the rest of the project (README,
> config, entities) — calls the device the **controller**. Danfoss's own firmware and
> documentation name it the **"master controller"** (and the rails it aggregates **"slave"**), so
> if you dig into the firmware or Danfoss's materials you'll meet *master/slave* — the same
> devices under their lower-level names.

## Roles

On the RJ45 link the **node (App Module) is the initiator** and the **controller is the responder**:

- The node sends **read** and **write** commands; the controller replies to each one.
- The controller never initiates traffic on this link — it owns all room state and simply answers.

> The controller is *also* a poller on a **separate** sub-GHz mesh to the thermostats, using nearly
> identical framing. Don't conflate the two links: on the RJ45 the controller only ever responds.

## Physical layer

- **Full-duplex RS-485**, two differential pairs (see [HARDWARE.md](HARDWARE.md) for pinout).
- **115200 baud, 8-N-1.**

## Transaction model

The link is electrically full-duplex (separate pairs), but the protocol is strictly
request/response: the node keeps **one transaction outstanding** — it sends a command and waits for
the matching `0x0D` (or a timeout) before sending the next. Replies are matched to commands purely
by **order** (there is no index/attribute echo). The controller never sends anything unsolicited,
so the link is treated as up only while replies keep arriving — prolonged silence is the only
"disconnected" signal.

## Frame format

Commands and responses share a 4-byte header (`sync`, `len`, `0x01`, `cmd`), then diverge:

```text
command   01 [len] 01 [cmd] [idx]     [counts] [attr_id …] [value …]  [crc_lo] [crc_hi]
response  01 [len] 01 [cmd] [status]  [value …]                       [crc_lo] [crc_hi]
```

- `sync` (`0x01`) — start of frame.
- `len` — TOTAL frame length in bytes (`sync` … `crc_hi` inclusive). Structural minimum 7 bytes
  (an empty-value response); frames run up to ~61 in practice.
- `0x01` — fixed frame-type byte.
- `cmd` — command byte (see [command set](#command-set)).
- `idx` (command only) — device/room index.
- `status` (response only) — `0x00` = OK; non-zero = rejected (see [response status codes](#response-status-codes)).
- `counts` (command only) — read count in the high nibble ("n2"), write count in the low ("n1").
- `attr_id …` (command only) — (n1 + n2) × uint16, **big-endian**.
- `value …` — the write payload (after the id list), or the value a response returns.
  **Big-endian.**
- `crc_lo crc_hi` — CRC-16/MODBUS (init `0xFFFF`, reflected poly `0xA001`) over every byte before
  the two CRC bytes; **little-endian** on the wire.

## Command set

| Command | Direction | Meaning |
| --- | --- | --- |
| **`0x0C`** | node → controller | **command**: read (uses the `n2` count) or write (uses the `n1` count) |
| **`0x0D`** | controller → node | **response**: `[status=00][value…]`, matched to the command by order |
| `0x30` / `0x32` / `0x34` | node → controller | firmware-update sub-protocol (arm / data block / verify) |

The `0x30`/`0x32`/`0x34` commands are how the *original* App Module flashes the controller's firmware
over this same link. **This project never sends them** — it only reads and writes attributes, and
cannot flash the controller.

## Response status codes

The `status` byte of a `0x0D` reply is `0x00` on success; a non-zero value means the controller
rejected the command. These are the codes it returns on the attribute read/write path:

| Status | Meaning |
| --- | --- |
| `0x00` | OK |
| `0x02` | attribute not found — on a **read** (the controller has no such attribute) |
| `0x03` | attribute not found — on a **write** |
| `0x04` | value **out of range** (failed the attribute's min/max bounds) |
| `0x05` | attribute is **read-only / protected** |
| `0x07` | **length error** — the value is shorter than the attribute's size |

In practice the two you'll see are `0x04` (e.g. a setpoint outside the room's configured
min/max) and `0x05` (writing an identity or diagnostic attribute). The hub logs the decoded reason
on a rejected read/write.

## Read

```text
node → controller:  01 [len] 01 0C [idx] [n2<<4] [attr_id …]
controller → node:  01 [len] 01 0D 00 [value …]

example
node → controller:  01 0a 01 0c 31 10 03 00 35 de      (read room idx 0x31, attr 0x0300)
controller → node:  01 09 01 0d 00 09 8a b3 c2          (value 0x098a = 24.42 °C)
```

A read lists attribute IDs only (the `n2`/read count); the controller returns the value(s) it owns,
matched to the request **by order** (the response carries no attribute echo). Multiple IDs can be
batched into one command — e.g. the boot identity read.

## Write

```text
node → controller:  01 [len] 01 0C [idx] [n1] [attr_id] [value …]

example
node → controller:  01 0c 01 0c 31 01 05 09 08 66 ..crc  (set room 0x31 attr 0x0509 = 0x0866 = 21.50 °C)
```

A write uses the `n1`/primary count. The controller applies it subject to the attribute's
writable flag and its min/max range check — out-of-range writes are rejected, so send in-range
values.

## Device index — system topology

The `idx` byte of a command (shown in the frame layout above) selects which entity it addresses:

| Index | Entity |
| --- | --- |
| `0x00` | global / system identity |
| `0x01–0x03` | controllers (rails) — primary + up to two secondary |
| `0x04–0x30` | actuator outputs |
| **`0x31–0x5D`** | **room thermostats** — Room *n* on controller *c* is `0x31 + (c-1)*15 + (n-1)` |

The **same attribute ID means different things depending on the entity class** — e.g. `0x0300`
is a room attribute, `0x030C` an output attribute. Always treat an attribute as the pair
(index-class, attribute-id).

## Value encoding

- **Temperatures / setpoints** — `uint16` big-endian, **×100 °C** (`0x08FC` = 23.00, `0x0708` =
  18.00). **`0x8000` = invalid / no-sensor / unconfigured** sentinel.
- **Version strings** — length-prefixed ASCII (`05 '6' '.' '0' '4' 00` → "6.04").
- **`u8`** — enum / flag / percentage. **`u32`** — timestamps / serial-like values.
- **Schedule blocks** (`0x100C–0x1012`, Mon–Sun) — 13-byte records.

Because a response carries no attribute echo, the node slices the reply into values using the
known **byte size of each requested attribute**, in request order. The sizes for the attributes
this component uses are encoded in `protocol.h` (`attr_value_size`).

## Bring-up sequence

At boot the node just starts issuing reads and the controller answers. The original App Module's
startup sequence (which this project mirrors as needed) is:

1. Read a one-time identity bulk from idx 0 (revision, serial, software version, etc.).
2. Sweep per-slot identity across the controllers (versions, presence).
3. Settle into steady-state attribute polling across all rooms and outputs.

## Attributes used

The complete set this project reads and writes, grouped by entity class. The same numeric id means
different things per class, so read each table as `(class, attribute)`.

**Room** (idx `0x31`–`0x5D`):

| Attribute | Meaning | Notes |
| --- | --- | --- |
| `0x0300` | room (air) temperature | ×100 °C |
| `0x0304` | floor temperature | ×100 °C |
| `0x030A` | floor-sensor / regulation mode | 0 Comfort, 1 Floor, 2 Dual |
| `0x030F` | thermostat battery | percent; `0xFE` = low, `0xFF` = wired |
| `0x03F0` | error code | 16-bit fault bitmask (room faults) |
| `0x0507` / `0x0508` | setpoint min / max | per-room limits, **writable** |
| `0x0509` | home setpoint | **writable** — the HA-controlled target |
| `0x050C` / `0x050D` | floor temp min / max | floor clamp limits, **writable** |
| `0x007F` | device firmware version | length-prefixed ASCII |
| `0x0080` | device descriptor | carries the thermostat product id |
| `0x100A` | room mode | 0 AtHome, Away, Asleep; reset to AtHome by `force_manual` |
| `0x100B` | room control | 0 manual, non-zero = running a schedule; reset to manual by `force_manual` |
| `0x1013` | heating/cooling state | drives the climate **action** (heating / idle) |
| `0x1020` / `0x1021` / `0x1022` | output-group bitmaps (slow / medium / fast) | union = which actuator channels serve the room |

**Controller / rail** (idx `0x01`–`0x03`) and **global** (idx `0x00`):

| Attribute | Meaning | Notes |
| --- | --- | --- |
| `0x0015` | revision | hardware version in bytes `[0:1]`, software version in `[2:3]` |
| `0x007F` | firmware version | length-prefixed ASCII |
| `0x03F0` | error code | 16-bit fault bitmask (rail/system faults) |
| `0x0016` | serial number | `u32`, global idx 0 — describes the primary controller |

**Output** (idx `0x04`–`0x30`) — read during the boot discovery probe and logged, **not** turned
into entities:

| Attribute | Meaning | Notes |
| --- | --- | --- |
| `0x1008` | output used-by-room | which room the actuator channel serves |
| `0x1200` | output state (auto) | algorithm-driven on/off |
| `0x030C` | output duty cycle | percent |
| `0x7040` / `0x7041` | rail outputs available / in-use | bitmaps |
