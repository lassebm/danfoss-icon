# Terminology: this component uses PRIMARY / SECONDARY CONTROLLER throughout (see Topology below).
# Danfoss's own firmware/docs call these the "master controller" and "slave" rails — we don't;
# don't reintroduce master/slave into the code.
import esphome.codegen as cg

# Aliased to avoid the same-named submodules (sensor.py / text_sensor.py / a future
# climate.py) shadowing these core-component imports as package attributes.
from esphome.components import (
    binary_sensor as binary_sensor_,
    button as button_,
    climate as climate_,
    number as number_,
    sensor as sensor_,
    text_sensor as text_sensor_,
    uart,
)
import esphome.config_validation as cv
from esphome.const import (
    CONF_DEVICE_ID,
    CONF_ID,
    CONF_NAME,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_PROBLEM,
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_CONFIG,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_PERCENT,
)

# Core sub-device class — entities reference a top-level `devices:` entry via device_id,
# which groups them as a distinct device in Home Assistant (and lets it be area-assigned).
Device = cg.esphome_ns.class_("Device")

CODEOWNERS = ["@lassebm"]
DEPENDENCIES = ["uart"]
# Pull in the climate/sensor base so the hub can create per-room entities without the
# consumer declaring a `climate:` / `sensor:` platform block.
AUTO_LOAD = ["climate", "sensor", "text_sensor", "button", "binary_sensor", "number"]
MULTI_CONF = True

danfoss_icon_ns = cg.esphome_ns.namespace("danfoss_icon")
DanfossIconHub = danfoss_icon_ns.class_("DanfossIconHub", cg.Component, uart.UARTDevice)
DanfossIconClimate = danfoss_icon_ns.class_(
    "DanfossIconClimate", climate_.Climate, cg.Component
)
DanfossIconSensor = danfoss_icon_ns.class_(
    "DanfossIconSensor", sensor_.Sensor, cg.Component
)
DanfossIconTextSensor = danfoss_icon_ns.class_(
    "DanfossIconTextSensor", text_sensor_.TextSensor, cg.Component
)
DanfossIconYamlButton = danfoss_icon_ns.class_("DanfossIconYamlButton", button_.Button)
DanfossIconNumber = danfoss_icon_ns.class_(
    "DanfossIconNumber", number_.Number, cg.Component
)
# Problem rollup binary (device_class problem) over a room/controller 0x03F0 — see status.h.
DanfossIconProblem = danfoss_icon_ns.class_(
    "DanfossIconProblem", binary_sensor_.BinarySensor, cg.Component
)
DiSensorDecode = danfoss_icon_ns.enum("DiSensorDecode")
DiTextDecode = danfoss_icon_ns.enum("DiTextDecode")

# Referenced by the generic sensor/text_sensor sub-platforms (the explicit fallback).
CONF_DANFOSS_ICON_ID = "danfoss_icon_id"

# --- User-facing option names ---
# Hub-level
CONF_ROOMS = "rooms"
CONF_SECONDARY_CONTROLLERS = "secondary_controllers"
CONF_POLL_INTERVAL = "poll_interval"
CONF_REPLY_TIMEOUT = "reply_timeout"
CONF_CONNECTION_TIMEOUT = "connection_timeout"
CONF_FORCE_MANUAL = "force_manual"
CONF_DISCOVER_BUTTON = "discover_button"
# Per-room / per-controller fields and entity toggles
# A room's controller (rail position): 1 = primary, 2/3 = secondary
CONF_CONTROLLER = "controller"
CONF_NUMBER = "number"
CONF_BATTERY = "battery"
CONF_MODEL = "model"
CONF_FIRMWARE = "firmware"
CONF_HW_VERSION = "hardware_version"
CONF_SW_VERSION = "software_version"
CONF_SERIAL = "serial"
CONF_FLOOR = "floor"
CONF_OUTPUTS = "outputs"
# Editable per-room setpoint min/max (0x0507/0x0508)
CONF_SETPOINT_LIMITS = "setpoint_limits"
CONF_FAULT = "fault"

# --- Generated entity IDs ---
# Per room
CONF_CLIMATE_ID = "climate_id"
CONF_BATTERY_ID = "battery_id"
CONF_MODEL_ID = "model_id"
CONF_FIRMWARE_ID = "firmware_id"
CONF_ROOMTEMP_ID = "roomtemp_id"
CONF_FLOOR_ID = "floor_id"
CONF_FLOOR_MODE_ID = "floor_mode_id"
CONF_OUTPUTS_ID = "outputs_id"
CONF_SETPOINT_MIN_ID = "setpoint_min_id"
CONF_SETPOINT_MAX_ID = "setpoint_max_id"
CONF_FLOOR_MIN_ID = "floor_min_id"
CONF_FLOOR_MAX_ID = "floor_max_id"
CONF_ROOM_FAULT_ID = "room_fault_id"
CONF_ROOM_PROBLEM_ID = "room_problem_id"
# Primary controller (= this node device) identity entity IDs.
CONF_CTRL_FW_ID = "ctrl_fw_id"
CONF_CTRL_HW_ID = "ctrl_hw_id"
CONF_CTRL_SW_ID = "ctrl_sw_id"
CONF_CTRL_SERIAL_ID = "ctrl_serial_id"
CONF_CTRL_CONN_ID = "ctrl_conn_id"
CONF_CTRL_FAULT_ID = "ctrl_fault_id"
CONF_CTRL_PROBLEM_ID = "ctrl_problem_id"
# Secondary controller identity entity IDs.
CONF_SEC_FW_ID = "sec_fw_id"
CONF_SEC_HW_ID = "sec_hw_id"
CONF_SEC_SW_ID = "sec_sw_id"
CONF_SEC_FAULT_ID = "sec_fault_id"
CONF_SEC_PROBLEM_ID = "sec_problem_id"
# Helper button
CONF_DISCOVER_BUTTON_ID = "discover_button_id"

# Topology: up to 3 controllers on the bus (1 primary + up to 2 secondary controllers), 15 rooms
# each. A room is addressed by controller (rail position 1..3) + number 1..15; the flat wire index
# is idx = 0x31 + (controller-1)*15 + (number-1). Controllers (rails) sit at idx 1..3 = their number
# (1 = the primary controller this node is wired to; 2/3 = secondary controllers it aggregates).
ROOM_IDX_BASE = 0x31
SLOTS_PER_CONTROLLER = 15
ROOM_BATTERY_ATTR = 0x030F  # thermostat battery %
ROOM_MODEL_ATTR = 0x0080  # device descriptor (carries the product id)
ROOM_FIRMWARE_ATTR = 0x007F  # device firmware version string
ROOM_TEMP_ATTR = 0x0300  # room air temperature
ROOM_SETPOINT_ATTR = 0x0509  # active/home setpoint (the HA target)
ROOM_HEATCOOL_ATTR = 0x1013  # heating/cooling state (climate action)
ROOM_FLOOR_TEMP_ATTR = 0x0304  # floor temperature
ROOM_FLOOR_MODE_ATTR = 0x030A  # floor-sensor mode: Comfort/Floor/Dual
ROOM_SETPOINT_MIN_ATTR = 0x0507  # per-room setpoint lower limit (menu-configurable)
ROOM_SETPOINT_MAX_ATTR = 0x0508  # per-room setpoint upper limit (menu-configurable)
ROOM_FLOOR_MIN_ATTR = 0x050C  # floor temperature lower clamp limit
ROOM_FLOOR_MAX_ATTR = 0x050D  # floor temperature upper (overheat) clamp limit
# Output actuator state is three per-controller speed-category bitmaps; the Outputs text sensor
# reads the union (see DI_TEXT_OUTPUTS), so all three must be polled together.
ROOM_OUTPUTS_ATTR = 0x1020  # output bitmap, SLOW group
ROOM_OUTPUTS_MED_ATTR = 0x1021  # output bitmap, MEDIUM group
ROOM_OUTPUTS_FAST_ATTR = 0x1022  # output bitmap, FAST group
ROOM_ERRORCODE_ATTR = 0x03F0  # fault/error-code bitmask
CONTROLLER_REVISION_ATTR = 0x0015  # hardware + software revision (u16 each)
CONTROLLER_FIRMWARE_ATTR = 0x007F  # firmware version string
# The GLOBAL idx0 identity (serial 0x0016, etc.) describes only the PRIMARY controller (the one the
# emulator is wired to = rail idx 1); secondary controllers 2/3 expose only their per-rail identity.
# So serial is a primary-controller entity, read from idx0 (polled only when the Serial entity is on).
GLOBAL_IDX = 0x00  # global / system identity slot
SYSTEM_SERIAL_ATTR = 0x0016  # controller serial number


def _room_index(room):
    return (
        ROOM_IDX_BASE
        + (room[CONF_CONTROLLER] - 1) * SLOTS_PER_CONTROLLER
        + (room[CONF_NUMBER] - 1)
    )


# Entity IDs are declared here (in the schema), so ESPHome's validation pass registers them
# into component_ids automatically — no manual bookkeeping needed in to_code().
ROOM_SCHEMA = cv.Schema(
    {
        # Control
        cv.GenerateID(CONF_CLIMATE_ID): cv.declare_id(DanfossIconClimate),
        # Config (editable numbers)
        cv.GenerateID(CONF_SETPOINT_MIN_ID): cv.declare_id(DanfossIconNumber),
        cv.GenerateID(CONF_SETPOINT_MAX_ID): cv.declare_id(DanfossIconNumber),
        # Diagnostics
        cv.GenerateID(CONF_BATTERY_ID): cv.declare_id(DanfossIconSensor),
        cv.GenerateID(CONF_MODEL_ID): cv.declare_id(DanfossIconTextSensor),
        cv.GenerateID(CONF_FIRMWARE_ID): cv.declare_id(DanfossIconTextSensor),
        cv.GenerateID(CONF_OUTPUTS_ID): cv.declare_id(DanfossIconTextSensor),
        cv.GenerateID(CONF_ROOM_FAULT_ID): cv.declare_id(DanfossIconTextSensor),
        cv.GenerateID(CONF_ROOM_PROBLEM_ID): cv.declare_id(DanfossIconProblem),
        # Floor feature set (floor: true)
        cv.GenerateID(CONF_FLOOR_ID): cv.declare_id(DanfossIconSensor),
        cv.GenerateID(CONF_ROOMTEMP_ID): cv.declare_id(DanfossIconSensor),
        cv.GenerateID(CONF_FLOOR_MODE_ID): cv.declare_id(DanfossIconTextSensor),
        cv.GenerateID(CONF_FLOOR_MIN_ID): cv.declare_id(DanfossIconNumber),
        cv.GenerateID(CONF_FLOOR_MAX_ID): cv.declare_id(DanfossIconNumber),
        cv.Required(CONF_NUMBER): cv.int_range(min=1, max=15),
        cv.Required(CONF_NAME): cv.string,
        # Which controller this room is wired to (rail position): 1 = primary (this node),
        # 2/3 = a secondary controller. Default 1 — the common single-controller case.
        cv.Optional(CONF_CONTROLLER, default=1): cv.int_range(min=1, max=3),
        # Optional HA sub-device (declared under top-level `devices:`). When set, this room's
        # entities group under it and use bare names (the device supplies the room label).
        cv.Optional(CONF_DEVICE_ID): cv.use_id(Device),
        # Entity toggles — same order as the entities are created in to_code().
        # Editable setpoint min/max (0x0507/0x0508) as two config Number entities. These bound the
        # room's allowed setpoint (and the climate's slider range/clamp). Default on.
        cv.Optional(CONF_SETPOINT_LIMITS, default=True): cv.boolean,
        cv.Optional(CONF_BATTERY, default=True): cv.boolean,
        cv.Optional(CONF_MODEL, default=True): cv.boolean,
        cv.Optional(CONF_FIRMWARE, default=True): cv.boolean,
        cv.Optional(CONF_OUTPUTS, default=True): cv.boolean,
        # Fault: decoded text of the room 0x03F0 error code — "OK" or the active fault(s)
        # (Thermostat missing / RT touch error / Floor sensor short / Floor sensor disconnected).
        cv.Optional(CONF_FAULT, default=True): cv.boolean,
        # Floor sensor fitted? Opt-in per room. Enables the floor feature set: floor temp sensor,
        # Floor Sensor Mode (0x030A: Comfort/Floor/Dual), and editable Floor Min/Max (0x050C/0x050D).
        # In Floor mode the climate's current temp tracks the floor sensor.
        cv.Optional(CONF_FLOOR, default=False): cv.boolean,
    }
)

# Secondary controllers at idx 2..3 — optional. The primary controller
# (rail idx 1) is implicit = this node device (its identity entities are always created in
# to_code). Each secondary exposes its own identity diagnostics; serial/connection are NOT here —
# serial is the idx0 GLOBAL primary value, and the link we sense is to the primary (it aggregates
# the secondaries). `number` is the rail position: 2 = first secondary, 3 = second.
SECONDARY_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_SEC_FW_ID): cv.declare_id(DanfossIconTextSensor),
        cv.GenerateID(CONF_SEC_HW_ID): cv.declare_id(DanfossIconTextSensor),
        cv.GenerateID(CONF_SEC_SW_ID): cv.declare_id(DanfossIconTextSensor),
        cv.GenerateID(CONF_SEC_FAULT_ID): cv.declare_id(DanfossIconTextSensor),
        cv.GenerateID(CONF_SEC_PROBLEM_ID): cv.declare_id(DanfossIconProblem),
        cv.Required(CONF_NUMBER): cv.int_range(min=2, max=3),
        cv.Required(CONF_NAME): cv.string,
        cv.Optional(CONF_DEVICE_ID): cv.use_id(Device),
        cv.Optional(CONF_FIRMWARE, default=True): cv.boolean,
        cv.Optional(CONF_HW_VERSION, default=True): cv.boolean,
        cv.Optional(CONF_SW_VERSION, default=True): cv.boolean,
        # System fault from the rail's 0x03F0 error code high-byte flags: expansion/radio/command/
        # primary-controller/secondary-1/secondary-2 missing, Pt1000 short/open, plus output error.
        cv.Optional(CONF_FAULT, default=True): cv.boolean,
    }
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(DanfossIconHub),
            cv.GenerateID(CONF_DISCOVER_BUTTON_ID): cv.declare_id(
                DanfossIconYamlButton
            ),
            # Primary controller identity entities live on this node device. Their IDs are declared
            # here for auto-registration; each is created unless its toggle below is set false.
            cv.GenerateID(CONF_CTRL_FW_ID): cv.declare_id(DanfossIconTextSensor),
            cv.GenerateID(CONF_CTRL_HW_ID): cv.declare_id(DanfossIconTextSensor),
            cv.GenerateID(CONF_CTRL_SW_ID): cv.declare_id(DanfossIconTextSensor),
            cv.GenerateID(CONF_CTRL_SERIAL_ID): cv.declare_id(DanfossIconTextSensor),
            cv.GenerateID(CONF_CTRL_FAULT_ID): cv.declare_id(DanfossIconTextSensor),
            cv.GenerateID(CONF_CTRL_PROBLEM_ID): cv.declare_id(DanfossIconProblem),
            cv.GenerateID(CONF_CTRL_CONN_ID): cv.declare_id(
                binary_sensor_.BinarySensor
            ),
            # Primary-controller diagnostic toggles (mirror the per-secondary options). fault covers
            # both the Fault text and the Problem alarm. Connection has no toggle — link health is
            # core to the integration, so it is always created.
            cv.Optional(CONF_FIRMWARE, default=True): cv.boolean,
            cv.Optional(CONF_HW_VERSION, default=True): cv.boolean,
            cv.Optional(CONF_SW_VERSION, default=True): cv.boolean,
            cv.Optional(CONF_SERIAL, default=True): cv.boolean,
            cv.Optional(CONF_FAULT, default=True): cv.boolean,
            cv.Optional(CONF_DISCOVER_BUTTON, default=True): cv.boolean,
            # How long the controller can be silent before the Connection sensor reports
            # disconnected (there's no "link up" bit — the link is inferred from reply silence).
            cv.Optional(
                CONF_CONNECTION_TIMEOUT, default="15s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_ROOMS, default=list): cv.ensure_list(ROOM_SCHEMA),
            # Secondary controllers — optional; the primary is implicit (= this node device).
            cv.Optional(CONF_SECONDARY_CONTROLLERS, default=list): cv.ensure_list(
                SECONDARY_SCHEMA
            ),
            cv.Optional(
                CONF_POLL_INTERVAL, default="2s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_REPLY_TIMEOUT, default="500ms"
            ): cv.positive_time_period_milliseconds,
            # On boot, force any room found running a schedule (room control 0x100B != 0) back to
            # manual + AtHome so HA owns the active setpoint. Default on — the emulator's scope is
            # manual control; set false to leave the controller's native schedule untouched.
            cv.Optional(CONF_FORCE_MANUAL, default=True): cv.boolean,
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)

# Schemas to fill defaults for the entity configs we build per room from the pre-declared IDs.
_GEN_CLIMATE_SCHEMA = climate_.climate_schema(DanfossIconClimate).extend(
    cv.COMPONENT_SCHEMA
)
_GEN_BATTERY_SCHEMA = sensor_.sensor_schema(
    DanfossIconSensor,
    unit_of_measurement=UNIT_PERCENT,
    accuracy_decimals=0,
    device_class=DEVICE_CLASS_BATTERY,
    state_class=STATE_CLASS_MEASUREMENT,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
).extend(cv.COMPONENT_SCHEMA)
_GEN_FLOOR_SCHEMA = sensor_.sensor_schema(
    DanfossIconSensor,
    unit_of_measurement=UNIT_CELSIUS,
    accuracy_decimals=1,
    device_class=DEVICE_CLASS_TEMPERATURE,
    state_class=STATE_CLASS_MEASUREMENT,
).extend(cv.COMPONENT_SCHEMA)
_GEN_TEXT_SCHEMA = text_sensor_.text_sensor_schema(
    DanfossIconTextSensor, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
).extend(cv.COMPONENT_SCHEMA)
_GEN_BUTTON_SCHEMA = button_.button_schema(
    DanfossIconYamlButton, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
)
_GEN_CONN_SCHEMA = binary_sensor_.binary_sensor_schema(
    device_class=DEVICE_CLASS_CONNECTIVITY,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
)
# Problem = the fault alarm (any 0x03F0 fault), device_class "problem". Marked diagnostic to match
# Battery / Connection / Fault: HA groups these under Diagnostic on the device page and leaves them
# off the auto-generated Overview. It stays available for automations and dashboards you build.
_GEN_PROBLEM_SCHEMA = binary_sensor_.binary_sensor_schema(
    DanfossIconProblem,
    device_class=DEVICE_CLASS_PROBLEM,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
).extend(cv.COMPONENT_SCHEMA)
# Editable setpoint limit (0x0507/0x0508): a config-category temperature Number, slider 5–35 °C.
_GEN_SETPOINT_LIMIT_SCHEMA = number_.number_schema(
    DanfossIconNumber,
    unit_of_measurement=UNIT_CELSIUS,
    device_class=DEVICE_CLASS_TEMPERATURE,
    entity_category=ENTITY_CATEGORY_CONFIG,
).extend(cv.COMPONENT_SCHEMA)


def _cfg(id_obj, name, device_id):
    d = {CONF_ID: id_obj, CONF_NAME: name}
    if device_id is not None:
        d[CONF_DEVICE_ID] = device_id  # entity groups under the HA sub-device
    return d


async def _new_room_climate(hub, room, name, device_id):
    cfg = _GEN_CLIMATE_SCHEMA(_cfg(room[CONF_CLIMATE_ID], name, device_id))
    var = await climate_.new_climate(cfg)
    await cg.register_component(var, cfg)
    cg.add(var.set_parent(hub))
    idx = _room_index(room)
    cg.add(var.set_room_index(idx))
    # Live climate values — fast tier: air temp, active setpoint, heat/cool state. (Floor temp, the
    # current_temperature source in Floor mode, is registered fast by the floor feature set below;
    # the regulation-mode selector 0x030A is polled there too, gated to floor rooms.)
    cg.add(hub.add_fast_attr(idx, ROOM_TEMP_ATTR))
    cg.add(hub.add_fast_attr(idx, ROOM_SETPOINT_ATTR))
    cg.add(hub.add_fast_attr(idx, ROOM_HEATCOOL_ATTR))
    # Per-room setpoint min/max (0x0507/0x0508) drive the climate's visual bounds + clamp. Slow tier
    # (rarely change — only via thermostat menu/app); HA picks up new visual bounds on reconnect.
    cg.add(hub.add_slow_attr(idx, ROOM_SETPOINT_MIN_ATTR))
    cg.add(hub.add_slow_attr(idx, ROOM_SETPOINT_MAX_ATTR))


async def _new_room_battery(hub, room, name, device_id):
    cfg = _GEN_BATTERY_SCHEMA(_cfg(room[CONF_BATTERY_ID], name, device_id))
    var = await sensor_.new_sensor(cfg)
    await cg.register_component(var, cfg)
    cg.add(var.set_parent(hub))
    cg.add(var.set_index(_room_index(room)))
    cg.add(var.set_attribute(ROOM_BATTERY_ATTR))
    cg.add(var.set_decode(DiSensorDecode.DI_DECODE_BATTERY))


async def _new_room_setpoint_limit(
    hub,
    id_obj,
    name,
    idx,
    attribute,
    device_id,
    min_value=5.0,
    max_value=35.0,
    step=0.5,
):
    # The number's setup() registers the attr for polling (slow tier) and the room — dedup handles
    # the overlap with the climate, which also reads 0x0507/0x0508 for its bounds.
    cfg = _GEN_SETPOINT_LIMIT_SCHEMA(_cfg(id_obj, name, device_id))
    var = await number_.new_number(
        cfg, min_value=min_value, max_value=max_value, step=step
    )
    await cg.register_component(var, cfg)
    cg.add(var.set_parent(hub))
    cg.add(var.set_index(idx))
    cg.add(var.set_attribute(attribute))


async def _new_room_floor(hub, room, name, device_id):
    cfg = _GEN_FLOOR_SCHEMA(_cfg(room[CONF_FLOOR_ID], name, device_id))
    var = await sensor_.new_sensor(cfg)
    await cg.register_component(var, cfg)
    cg.add(var.set_parent(hub))
    cg.add(var.set_index(_room_index(room)))
    cg.add(var.set_attribute(ROOM_FLOOR_TEMP_ATTR))
    cg.add(var.set_decode(DiSensorDecode.DI_DECODE_TEMP))
    # The sensor's setup() registers 0x0304 (slow); the floor feature set upgrades it to fast.


async def _new_room_temp(hub, room, name, device_id):
    # Standalone air-temp (0x0300) sensor. The climate's current_temperature shows the *regulated*
    # sensor (floor in Floor mode), so in Floor mode the air reading would otherwise be invisible —
    # this keeps it available, mirroring the app's "advanced readings" (both sensors always shown).
    cfg = _GEN_FLOOR_SCHEMA(_cfg(room[CONF_ROOMTEMP_ID], name, device_id))
    var = await sensor_.new_sensor(cfg)
    await cg.register_component(var, cfg)
    cg.add(var.set_parent(hub))
    cg.add(var.set_index(_room_index(room)))
    cg.add(var.set_attribute(ROOM_TEMP_ATTR))
    cg.add(
        var.set_decode(DiSensorDecode.DI_DECODE_TEMP)
    )  # 0x0300 already in the fast poll set


async def _new_room_fault(hub, room, prefix, device_id):
    # `fault` makes two entities (Problem alarm + Fault text), both reading 0x03F0; the Fault text
    # sensor's setup() registers the poll, which the Problem binary piggybacks on.
    idx = _room_index(room)
    await _new_problem(
        hub, room[CONF_ROOM_PROBLEM_ID], f"{prefix}Problem", idx, device_id
    )
    cfg = _GEN_TEXT_SCHEMA(_cfg(room[CONF_ROOM_FAULT_ID], f"{prefix}Fault", device_id))
    var = await text_sensor_.new_text_sensor(cfg)
    await cg.register_component(var, cfg)
    cg.add(var.set_parent(hub))
    cg.add(var.set_index(idx))
    cg.add(var.set_attribute(ROOM_ERRORCODE_ATTR))
    cg.add(var.set_decode(DiTextDecode.DI_TEXT_FAULT_ROOM))


async def _new_problem(hub, id_obj, name, index, device_id=None):
    # device_class problem binary over 0x03F0 (hardcoded in DanfossIconProblem): the HA-visible
    # fault alarm. Pairs with the Fault text sensor (same 0x03F0, already polled) for detail.
    cfg = _GEN_PROBLEM_SCHEMA(_cfg(id_obj, name, device_id))
    var = await binary_sensor_.new_binary_sensor(cfg)
    await cg.register_component(var, cfg)
    cg.add(var.set_parent(hub))
    cg.add(var.set_index(index))


async def _new_text(hub, id_obj, name, index, attribute, decode, device_id=None):
    cfg = _GEN_TEXT_SCHEMA(_cfg(id_obj, name, device_id))
    var = await text_sensor_.new_text_sensor(cfg)
    await cg.register_component(var, cfg)
    cg.add(var.set_parent(hub))
    cg.add(var.set_index(index))
    cg.add(var.set_attribute(attribute))
    cg.add(var.set_decode(decode))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_poll_interval(config[CONF_POLL_INTERVAL]))
    cg.add(var.set_reply_timeout(config[CONF_REPLY_TIMEOUT]))
    cg.add(var.set_force_manual(config[CONF_FORCE_MANUAL]))

    if config[CONF_DISCOVER_BUTTON]:
        bcfg = _GEN_BUTTON_SCHEMA(
            {CONF_ID: config[CONF_DISCOVER_BUTTON_ID], CONF_NAME: "Discover Config"}
        )
        btn = await button_.new_button(bcfg)
        cg.add(btn.set_parent(var))

    cg.add(var.set_link_timeout(config[CONF_CONNECTION_TIMEOUT]))

    for room in config[CONF_ROOMS]:
        idx = _room_index(room)
        dev = room.get(CONF_DEVICE_ID)
        # On a sub-device, entities use bare names and the climate inherits the device name;
        # otherwise they're prefixed with the room name and the climate carries it.
        prefix = "" if dev is not None else f"{room[CONF_NAME]} "
        climate_name = "" if dev is not None else room[CONF_NAME]
        await _new_room_climate(var, room, climate_name, dev)
        if room[CONF_SETPOINT_LIMITS]:
            await _new_room_setpoint_limit(
                var,
                room[CONF_SETPOINT_MIN_ID],
                f"{prefix}Setpoint Min",
                idx,
                ROOM_SETPOINT_MIN_ATTR,
                dev,
            )
            await _new_room_setpoint_limit(
                var,
                room[CONF_SETPOINT_MAX_ID],
                f"{prefix}Setpoint Max",
                idx,
                ROOM_SETPOINT_MAX_ATTR,
                dev,
            )
        if room[CONF_BATTERY]:
            await _new_room_battery(var, room, f"{prefix}Battery", dev)
        if room[CONF_MODEL]:
            await _new_text(
                var,
                room[CONF_MODEL_ID],
                f"{prefix}Model",
                idx,
                ROOM_MODEL_ATTR,
                DiTextDecode.DI_TEXT_PRODUCT,
                dev,
            )
        if room[CONF_FIRMWARE]:
            await _new_text(
                var,
                room[CONF_FIRMWARE_ID],
                f"{prefix}Firmware Version",
                idx,
                ROOM_FIRMWARE_ATTR,
                DiTextDecode.DI_TEXT_VERSTR,
                dev,
            )
        if room[CONF_OUTPUTS]:
            await _new_text(
                var,
                room[CONF_OUTPUTS_ID],
                f"{prefix}Outputs",
                idx,
                ROOM_OUTPUTS_ATTR,
                DiTextDecode.DI_TEXT_OUTPUTS,
                dev,
            )
            # The text sensor unions all three speed-group bitmaps. Its setup() registers the base
            # group (0x1020); register the other two so the whole union is polled.
            cg.add(var.add_slow_attr(idx, ROOM_OUTPUTS_MED_ATTR))
            cg.add(var.add_slow_attr(idx, ROOM_OUTPUTS_FAST_ATTR))
        if room[CONF_FAULT]:
            await _new_room_fault(var, room, prefix, dev)
        # Floor feature set — only when a floor sensor is fitted (floor: true). Per the thermostat
        # guide, the floor min/max apply in Comfort and Dual modes; the mode itself is only
        # meaningful with a floor sensor, so all three entities are gated together here.
        if room[CONF_FLOOR]:
            # Floor temp is the displayed current_temperature in Floor mode -> fast tier (upgrades
            # the floor sensor's default slow registration). The mode selector 0x030A is polled slow
            # by the Floor Sensor Mode text sensor below; both are thus gated to floor rooms.
            cg.add(var.add_fast_attr(idx, ROOM_FLOOR_TEMP_ATTR))
            await _new_room_floor(var, room, f"{prefix}Floor", dev)
            # Air temp as its own sensor so Floor mode (climate current = floor) doesn't hide it.
            await _new_room_temp(var, room, f"{prefix}Room Temperature", dev)
            await _new_text(
                var,
                room[CONF_FLOOR_MODE_ID],
                f"{prefix}Floor Sensor Mode",
                idx,
                ROOM_FLOOR_MODE_ATTR,
                DiTextDecode.DI_TEXT_FLOOR_MODE,
                dev,
            )
            await _new_room_setpoint_limit(
                var,
                room[CONF_FLOOR_MIN_ID],
                f"{prefix}Floor Setpoint Min",
                idx,
                ROOM_FLOOR_MIN_ATTR,
                dev,
            )
            await _new_room_setpoint_limit(
                var,
                room[CONF_FLOOR_MAX_ID],
                f"{prefix}Floor Setpoint Max",
                idx,
                ROOM_FLOOR_MAX_ATTR,
                dev,
            )

    # Primary controller (rail idx 1) = THIS node device: identity entities use bare names, so
    # "Danfoss Icon" represents the controller. Each is gated by its toggle (mirroring secondaries).
    # Serial is the idx0 GLOBAL value; Connection (link health) is always created.
    if config[CONF_FIRMWARE]:
        await _new_text(
            var,
            config[CONF_CTRL_FW_ID],
            "Firmware Version",
            1,
            CONTROLLER_FIRMWARE_ATTR,
            DiTextDecode.DI_TEXT_VERSTR,
        )
    if config[CONF_HW_VERSION]:
        await _new_text(
            var,
            config[CONF_CTRL_HW_ID],
            "Hardware Version",
            1,
            CONTROLLER_REVISION_ATTR,
            DiTextDecode.DI_TEXT_HWVER,
        )
    if config[CONF_SW_VERSION]:
        await _new_text(
            var,
            config[CONF_CTRL_SW_ID],
            "Software Version",
            1,
            CONTROLLER_REVISION_ATTR,
            DiTextDecode.DI_TEXT_SWVER,
        )
    if config[CONF_SERIAL]:
        await _new_text(
            var,
            config[CONF_CTRL_SERIAL_ID],
            "Serial",
            GLOBAL_IDX,
            SYSTEM_SERIAL_ATTR,
            DiTextDecode.DI_TEXT_SERIAL,
        )
    if config[CONF_FAULT]:
        await _new_problem(var, config[CONF_CTRL_PROBLEM_ID], "Problem", 1)
        await _new_text(
            var,
            config[CONF_CTRL_FAULT_ID],
            "Fault",
            1,
            ROOM_ERRORCODE_ATTR,
            DiTextDecode.DI_TEXT_FAULT_RAIL,
        )
    ccfg = _GEN_CONN_SCHEMA(_cfg(config[CONF_CTRL_CONN_ID], "Connection", None))
    conn = await binary_sensor_.new_binary_sensor(ccfg)
    cg.add(var.set_link_sensor(conn))

    # Secondary controllers at rail idx = number (2/3). Each groups under
    # its own HA sub-device if device_id is set, else its entities are name-prefixed on the node
    # device. No serial/connection — those are primary-only (see SECONDARY_SCHEMA).
    for sec in config[CONF_SECONDARY_CONTROLLERS]:
        idx = sec[CONF_NUMBER]  # rail idx 2/3
        dev = sec.get(CONF_DEVICE_ID)
        prefix = "" if dev is not None else f"{sec[CONF_NAME]} "
        if sec[CONF_FIRMWARE]:
            await _new_text(
                var,
                sec[CONF_SEC_FW_ID],
                f"{prefix}Firmware Version",
                idx,
                CONTROLLER_FIRMWARE_ATTR,
                DiTextDecode.DI_TEXT_VERSTR,
                dev,
            )
        if sec[CONF_HW_VERSION]:
            await _new_text(
                var,
                sec[CONF_SEC_HW_ID],
                f"{prefix}Hardware Version",
                idx,
                CONTROLLER_REVISION_ATTR,
                DiTextDecode.DI_TEXT_HWVER,
                dev,
            )
        if sec[CONF_SW_VERSION]:
            await _new_text(
                var,
                sec[CONF_SEC_SW_ID],
                f"{prefix}Software Version",
                idx,
                CONTROLLER_REVISION_ATTR,
                DiTextDecode.DI_TEXT_SWVER,
                dev,
            )
        if sec[CONF_FAULT]:
            await _new_problem(
                var, sec[CONF_SEC_PROBLEM_ID], f"{prefix}Problem", idx, dev
            )
            await _new_text(
                var,
                sec[CONF_SEC_FAULT_ID],
                f"{prefix}Fault",
                idx,
                ROOM_ERRORCODE_ATTR,
                DiTextDecode.DI_TEXT_FAULT_RAIL,
                dev,
            )
