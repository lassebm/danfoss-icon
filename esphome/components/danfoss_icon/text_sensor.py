import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_TYPE

from . import CONF_DANFOSS_ICON_ID, DanfossIconHub, DanfossIconTextSensor, DiTextDecode

DEPENDENCIES = ["danfoss_icon"]

CONF_INDEX = "index"
CONF_ATTRIBUTE = "attribute"

DECODE = {
    "verstr": DiTextDecode.DI_TEXT_VERSTR,
    "revision": DiTextDecode.DI_TEXT_REVISION,
    "hex": DiTextDecode.DI_TEXT_HEX,
    "product": DiTextDecode.DI_TEXT_PRODUCT,
    "serial": DiTextDecode.DI_TEXT_SERIAL,
    "outputs": DiTextDecode.DI_TEXT_OUTPUTS,
}

CONFIG_SCHEMA = (
    text_sensor.text_sensor_schema(DanfossIconTextSensor)
    .extend(
        {
            cv.GenerateID(CONF_DANFOSS_ICON_ID): cv.use_id(DanfossIconHub),
            cv.Required(CONF_INDEX): cv.int_range(min=0x00, max=0x5D),
            cv.Required(CONF_ATTRIBUTE): cv.hex_uint16_t,
            cv.Required(CONF_TYPE): cv.enum(DECODE, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await text_sensor.new_text_sensor(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_DANFOSS_ICON_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_index(config[CONF_INDEX]))
    cg.add(var.set_attribute(config[CONF_ATTRIBUTE]))
    cg.add(var.set_decode(config[CONF_TYPE]))
