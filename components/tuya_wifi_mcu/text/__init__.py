import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text
from esphome.const import CONF_ID, CONF_MAX_LENGTH, CONF_MIN_LENGTH

from .. import TuyaWifiMcuComponent

DEPENDENCIES = ["tuya_wifi_mcu"]

CONF_TUYA_WIFI_MCU_ID = "tuya_wifi_mcu_id"
CONF_DP_ID = "dp_id"

tuya_wifi_mcu_ns = cg.esphome_ns.namespace("tuya_wifi_mcu")
TuyaWifiMcuText = tuya_wifi_mcu_ns.class_("TuyaWifiMcuText", text.Text, cg.Component)

CONFIG_SCHEMA = (
    text.text_schema(TuyaWifiMcuText, mode="text")
    .extend(
        {
            cv.GenerateID(CONF_TUYA_WIFI_MCU_ID): cv.use_id(TuyaWifiMcuComponent),
            cv.Required(CONF_DP_ID): cv.int_range(min=0, max=255),
            cv.Optional(CONF_MIN_LENGTH, default=0): cv.int_range(min=0, max=255),
            cv.Optional(CONF_MAX_LENGTH, default=255): cv.int_range(min=0, max=255),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_TUYA_WIFI_MCU_ID])
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await text.register_text(
        var,
        config,
        min_length=config[CONF_MIN_LENGTH],
        max_length=config[CONF_MAX_LENGTH],
    )
    cg.add(var.set_dp_id(config[CONF_DP_ID]))
    cg.add(parent.register_tuya_wifi_mcu_entity(var))
