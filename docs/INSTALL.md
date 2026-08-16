# Installation

Get the node flashed, configured, and talking to Home Assistant.

## Prerequisites

- The **hardware built and wired** per [HARDWARE.md](HARDWARE.md).
- Either the **ESPHome Device Builder app** (add-on) in Home Assistant (the usual route for HA
  users), or the **ESPHome CLI** if you prefer a terminal.

## 1. Add the component

This is published as an ESPHome **external component** — no checkout required. Your device config
pulls it straight from this repo:

```yaml
external_components:
  - source: github://lassebm/danfoss-icon@main
    components: [danfoss_icon]
```

The bundled [`esphome/danfoss-icon.yaml`](../esphome/danfoss-icon.yaml) is a complete, working
example to copy from. It defaults to a **local** source (`type: local`, `path: components`) so it
builds directly inside a clone of this repo; to deploy from the published component, switch to the
`github://` source it keeps commented at the top of its `external_components:` block (the same one
shown above).

## 2. Create the device and set Wi-Fi

### Home Assistant — ESPHome Device Builder app (recommended)

1. Open the **ESPHome** dashboard → **＋ New Device**, name it (e.g. `danfoss-icon`).
2. **Edit** its YAML and paste in the `external_components`, `uart`, and `danfoss_icon` blocks
   (copy from the example, then edit your rooms — see [step 3](#3-configure-your-rooms)).
3. Set Wi-Fi in the dashboard's **Secrets** editor (top-right **⋮ → Secrets**): add `wifi_ssid`
   and `wifi_password` once, and every device's `!secret` references resolve against it.

### ESPHome CLI

The config uses `!secret`, so create a secrets file next to it:

```bash
cd esphome
cp secrets.yaml.example secrets.yaml   # then edit in your Wi-Fi credentials
```

`secrets.yaml` is gitignored, so your credentials never get committed.

## 3. Configure your rooms

The one part you must edit is the room list. Each room has a `number` (1–15 on its controller)
and a `name`:

```yaml
danfoss_icon:
  id: icon
  uart_id: controller_uart
  rooms:
    - number: 1
      name: "Kitchen"
    - number: 2
      name: "Bedroom"
```

Don't know your room numbers? Flash with a couple of placeholder rooms, then press the
**"Discover Config"** button — it logs a ready-to-paste `rooms:` block for your actual system
(see [step 5](#5-bring-up--discovery)).

**UART.** The bundled config already sets the UART to match [HARDWARE.md](HARDWARE.md); you only
need to touch it if you wired different pins. The 8-N-1 framing is ESPHome's default, so the
block is just:

```yaml
uart:
  id: controller_uart
  tx_pin: GPIO3
  rx_pin: GPIO4
  baud_rate: 115200
```

**Rooms as Home Assistant devices (optional).** To give each room its own area-assignable HA
device, declare the devices once and reference them per room with `device_id`:

```yaml
esphome:
  name: danfoss-icon
  devices:
    - id: dev_kitchen
      name: "Kitchen"

danfoss_icon:
  rooms:
    - number: 1
      name: "Kitchen"
      device_id: dev_kitchen
```

The **primary controller is this node itself** — its firmware / hardware / software versions,
serial, link status, and fault appear automatically on the node's device. Add
`secondary_controllers:` only if you have additional controllers linked to the primary.

### Configuration reference

#### `danfoss_icon:` (hub)

| Option | Default | Meaning |
| --- | --- | --- |
| `uart_id` | — | the UART configured above (required) |
| `poll_interval` | `2s` | how often to refresh room state |
| `reply_timeout` | `500ms` | how long to wait for a response before moving on |
| `connection_timeout` | `15s` | controller silence before the link reports disconnected |
| `all_rooms_mode` | `true` | create the hub-level **All Rooms Mode** select (Home/Away/Sleep/Off across every room) |
| `discover_button` | `true` | create the "Discover Config" helper button |
| `firmware`, `hardware_version`, `software_version`, `serial` | `true` | which primary-controller identity sensors to create |
| `fault` | `true` | primary-controller Fault text **and** Problem alarm |
| `rooms` | `[]` | your rooms (see below) |
| `secondary_controllers` | `[]` | additional linked controllers (see below) |

The primary controller's entities live on this node device. The **Connection** (link health) sensor
is always created; the toggles above hide the rest if you don't want them.

#### Per room

| Option | Default | Meaning |
| --- | --- | --- |
| `number` | — | room number 1–15 on its controller (required) |
| `name` | — | room name (required) |
| `controller` | `1` | rail position: 1 = primary (this node), 2/3 = a secondary |
| `device_id` | — | optional HA sub-device to group the room's entities under |
| `floor` | `false` | room has a floor sensor → adds floor temp, floor mode, floor min/max |
| `room_temperature` | `true` | air-temperature sensor |
| `setpoint` | `true` | setpoint sensor |
| `setpoint_limits` | `true` | expose editable Setpoint Min/Max number entities |
| `battery`, `model`, `firmware`, `outputs` | `true` | which diagnostic entities to create |
| `fault` | `true` | room Fault text **and** Problem alarm |

Room Temperature and Setpoint duplicate what the climate already shows, on purpose: Home Assistant
keeps long-term history for sensors but not for climate entities. Room Temperature is the *air*
reading, which in Floor mode the climate replaces with the floor probe.

#### Per secondary controller (`secondary_controllers:`)

A linked controller at rail position 2 or 3. It has no serial or connection sensor — those are
primary-only.

| Option | Default | Meaning |
| --- | --- | --- |
| `number` | — | rail position: 2 or 3 (required) |
| `name` | — | controller name (required) |
| `device_id` | — | optional HA sub-device to group its entities under |
| `firmware`, `hardware_version`, `software_version` | `true` | which identity sensors to create |
| `fault` | `true` | controller Fault text **and** Problem alarm |

## 4. Install / flash

First flash is over USB. Keep RJ45 **+5 V disconnected** while USB is connected — see the
power-safety note in [HARDWARE.md](HARDWARE.md).

- **App:** click **Install** on the device (plug in over USB for the first flash; later
  updates go over Wi-Fi / OTA).
- **CLI:** `esphome run danfoss-icon.yaml`

## 5. Bring-up & discovery

Watch the logs (**Logs** in the app, or `esphome logs danfoss-icon.yaml`). If wiring and
polarity are right, you'll see the controller's `0x0D` replies decoded with sensible values
(room temperatures, setpoints, …) — that confirms the link works in both directions.

Press the **"Discover Config"** button to log a ready-to-paste `rooms:` block enumerating the
rooms your controller actually reports, then copy it into your config.

## 6. Home Assistant

Home Assistant auto-discovers the node over the native ESPHome API; confirm/add the integration.
Each room shows up as a **climate** entity with **Home / Away / Sleep** presets — each preset has its
own setpoint, and the controller regulates to whichever is selected — plus the diagnostic sensors you
enabled. A hub-level **All Rooms Mode** select (Home / Away / Sleep / Off) also appears on the node
device and switches every room at once. Change a setpoint and confirm it takes effect on the
thermostat — that round-trips a write to the controller.

Every room is kept in manual mode so Home Assistant owns the mode and setpoint; the controller's own
weekly schedule is not used.

## Troubleshooting

- **No `0x0D` replies in the logs.** Check the RS-485 wiring first. The two pairs are
  direction-specific — if you see nothing, try **swapping the A/B (or the pair) connections**;
  reversed polarity is the most common cause. Confirm a common ground between the ESP32, both
  transceiver modules, and the RJ45.
- **Bad-CRC / garbled frames.** Usually noise or a marginal connection on a data pair; check
  the crimp/punch-down and keep the data leads short.
- **Link reports disconnected.** The node infers the link from reply silence — if polls aren't
  being answered, it's the same wiring/polarity check as above.
- **Setpoint write rejected.** The controller range-checks writes; make sure the value is within
  the room's configured Setpoint Min/Max.
- **Don't power from USB and RJ45 +5 V simultaneously** — see the power-safety section in
  [HARDWARE.md](HARDWARE.md).
