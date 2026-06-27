# Hardware

This describes how to build the ESP32 node that plugs into the **App** port on the
Icon controller and speaks its protocol. The link is **full-duplex RS-485 at 115200 8-N-1** — the controller drives one
differential pair, the node drives the other.

<p align="center">
  <img src="images/assembled-node.webp" alt="An assembled node — an ESP32-C3 wired to two RS-485 transceiver modules and an RJ45 breakout" width="480"><br>
  <em>Overview of an assembled node — see the pinout and wiring tables below for the exact connections.</em>
</p>

## Bill of materials

| Qty | Part | Notes |
| --- | --- | --- |
| 1 | **ESP32-C3** dev board | e.g. ESP32-C3 Super Mini or DevKitM-1. Any ESP32 with a spare hardware UART works; the example config targets an ESP32-C3. |
| 2 | **Half-duplex auto-direction RS-485 transceiver module** | The common "TTL ↔ RS-485, automatic direction control" breakout (no DE/RE enable pin to drive). **Two** of them form a full-duplex link — one receives, one transmits. |
| 1 | **RJ45 breakout / keystone + patch cable** | To tap the controller's **App** port. A punch-down keystone or a cut patch cable both work. |
| — | Jumper wires | Module ↔ ESP32 ↔ RJ45. |

> **Why two half-duplex modules instead of one full-duplex transceiver?** The link is genuinely
> full-duplex (two separate pairs, both can carry data at once). The cheap auto-direction modules
> are half-duplex, so one module is dedicated to the receive pair and the other to the transmit
> pair. This mirrors what the original App Module does with a single full-duplex transceiver, but
> with parts you can buy for a couple of dollars.
>
> **A single full-duplex transceiver works too** (e.g. SN65HVD71, MAX3491) — it has separate
> driver and receiver, so it exposes one TX (`DI`) and one RX (`RO`) pin to the MCU and needs **no
> direction control** (the auto-direction logic on the cheap modules only exists to arbitrate a
> shared half-duplex line, which full-duplex doesn't have). Wire `DI`←GPIO3, `RO`→GPIO4, the
> receive pair from RJ45 1/2 and the drive pair to RJ45 3/6. It's the tidier option if you can
> source one; the two-module approach is just cheaper and more commonly on hand.

## RJ45 controller-port pinout

The controller's RJ45 carries two differential pairs plus power and ground:

| Pin | Signal | Pair |
| --- | --- | --- |
| 1 | controller → node **+** (A) | orange pair |
| 2 | controller → node **−** (B) | orange pair |
| 3 | node → controller **+** (Y) | green pair |
| 6 | node → controller **−** (Z) | green pair |
| 4 | **+5 V** | power |
| 7 | **+5 V** | power (duplicate of pin 4) |
| 5 | **GND** | ground |
| 8 | **GND** | ground (duplicate of pin 5) |

- **Pins 1 & 2** (orange) = controller → node. The node *receives* on this pair.
- **Pins 3 & 6** (green) = node → controller. The node *transmits* on this pair.
- **Pins 4 & 7 are the same +5 V net; pins 5 & 8 are the same GND net** — each rail is duplicated
  across two conductors. At your breakout, **tie 4 ↔ 7 together and 5 ↔ 8 together**, then run a
  single +5 V and single GND on to the board so the load is shared across both conductors.

## Wiring

Two half-duplex auto-direction modules, used as a full-duplex pair:

| Module | RS-485 side | TTL side |
| --- | --- | --- |
| **RX module** (receive) | A ← RJ45 **pin 1**, B ← RJ45 **pin 2** | RXD → ESP32 **GPIO4** (UART RX); TXD unconnected (it never drives) |
| **TX module** (transmit) | A → RJ45 **pin 3**, B → RJ45 **pin 6** | TXD ← ESP32 **GPIO3** (UART TX); RXD unconnected |

Power and ground (see the bridging note above):

- RJ45 **+5 V (bridged pins 4 & 7)** → ESP32-C3 **`5V` / `VIN`** pin
- RJ45 **GND (bridged pins 5 & 8)** → ESP32-C3 **`GND`**, **and** to the `GND` of *both* transceiver
  modules — everything must share a common ground.
- The transceiver modules run from the ESP32's **3.3 V** rail; their logic levels then match the
  ESP32's 3.3 V GPIOs.

```text
  Signal and power (full pin detail is in the tables above):

  RJ45 pins 1,2  ──>  RX module  ──RXD─>  ESP32 GPIO4 (RX)
  RJ45 pins 3,6  <──  TX module  <─TXD──  ESP32 GPIO3 (TX)
  RJ45 pins 4,7  ──(+5 V, bridged)─────>  ESP32 5V / VIN
  RJ45 pins 5,8  ──(GND, bridged)──────>  ESP32 GND  (common)

  ESP32 3V3 + GND  ──>  powers both transceiver modules
```

The UART pins (GPIO3 TX / GPIO4 RX) are configurable in YAML; the ESP32-C3 routes UART through its
GPIO matrix, so any free pins work. Avoid the strapping pins (GPIO2, 8, 9) and the USB pins (18, 19).

## ⚠️ Power safety — do **not** back-feed +5 V and USB at the same time

On most ESP32-C3 dev boards the `5V`/`VIN` header pin is the **same net as USB VBUS** (no
isolation diode). Feeding RJ45 +5 V into `VIN` *while USB is also plugged in* parallels two 5 V
sources and back-feeds one into the other.

- **Bench bring-up (USB plugged in for logs):** leave RJ45 **+5 V (pins 4 & 7) disconnected**.
  Wire only the data pairs (1/2, 3/6) and GND (5/8). USB powers the ESP32-C3 and the modules.
- **Deployment (no USB):** power from RJ45 +5 V → `VIN`, as wired above.
- **Need both live at once:** Schottky-diode-OR the RJ45 +5 V and USB VBUS into `VIN`.

## Notes

- **3.3 V logic** on the transceiver's receive output (`RXD`, or `RO` on a bare chip) is safe for
  the ESP32.
- **Termination/bias** is generally unnecessary for the short cable runs typical here; add
  120 Ω termination across each pair only if you see signal-integrity issues on a long run.
