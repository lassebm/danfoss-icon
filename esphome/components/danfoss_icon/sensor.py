import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import CONF_TYPE

from . import CONF_DANFOSS_ICON_ID, DanfossIconHub, DanfossIconSensor, DiSensorDecode

DEPENDENCIES = ["danfoss_icon"]

CONF_INDEX = "index"
CONF_ATTRIBUTE = "attribute"

DECODE = {
    "battery": DiSensorDecode.DI_DECODE_BATTERY,
    "raw": DiSensorDecode.DI_DECODE_RAW_U8,
    "temperature": DiSensorDecode.DI_DECODE_TEMP,
    "u16": DiSensorDecode.DI_DECODE_U16,
}

CONFIG_SCHEMA = (
    sensor.sensor_schema(DanfossIconSensor)
    .extend(
        {
            cv.GenerateID(CONF_DANFOSS_ICON_ID): cv.use_id(DanfossIconHub),
            # Device index (0x00 global/identity, 0x31..0x5D rooms).
            cv.Required(CONF_INDEX): cv.int_range(min=0x00, max=0x5D),
            cv.Required(CONF_ATTRIBUTE): cv.hex_uint16_t,
            cv.Required(CONF_TYPE): cv.enum(DECODE, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_DANFOSS_ICON_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_index(config[CONF_INDEX]))
    cg.add(var.set_attribute(config[CONF_ATTRIBUTE]))
    cg.add(var.set_decode(config[CONF_TYPE]))
