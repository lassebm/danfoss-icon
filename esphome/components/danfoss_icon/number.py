import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import CONF_MAX_VALUE, CONF_MIN_VALUE, CONF_STEP, UNIT_CELSIUS

from . import CONF_DANFOSS_ICON_ID, DanfossIconHub, DanfossIconNumber

DEPENDENCIES = ["danfoss_icon"]

CONF_INDEX = "index"
CONF_ATTRIBUTE = "attribute"

# Writable temperature attribute (u16 BE ×100 °C), e.g. per-room setpoint min/max 0x0507/0x0508.
# min/max/step bound the HA slider; defaults match the controller's setpoint range.
CONFIG_SCHEMA = (
    number.number_schema(DanfossIconNumber, unit_of_measurement=UNIT_CELSIUS)
    .extend(
        {
            cv.GenerateID(CONF_DANFOSS_ICON_ID): cv.use_id(DanfossIconHub),
            # Device index (0x00 global/identity, 0x31..0x5D rooms).
            cv.Required(CONF_INDEX): cv.int_range(min=0x00, max=0x5D),
            cv.Required(CONF_ATTRIBUTE): cv.hex_uint16_t,
            cv.Optional(CONF_MIN_VALUE, default=5.0): cv.float_,
            cv.Optional(CONF_MAX_VALUE, default=35.0): cv.float_,
            cv.Optional(CONF_STEP, default=0.5): cv.positive_float,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await number.new_number(
        config,
        min_value=config[CONF_MIN_VALUE],
        max_value=config[CONF_MAX_VALUE],
        step=config[CONF_STEP],
    )
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_DANFOSS_ICON_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_index(config[CONF_INDEX]))
    cg.add(var.set_attribute(config[CONF_ATTRIBUTE]))
