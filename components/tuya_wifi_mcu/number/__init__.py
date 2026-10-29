import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_ID, CONF_MAX_VALUE, CONF_MIN_VALUE, CONF_STEP

from .. import TuyaWifiMcuComponent

DEPENDENCIES = ["tuya_wifi_mcu"]

CONF_TUYA_WIFI_MCU_ID = "tuya_wifi_mcu_id"
CONF_DP_ID = "dp_id"
CONF_SCALE = "scale"
CONF_BIND_NUMBER_ID = "bind_number_id"

tuya_wifi_mcu_ns = cg.esphome_ns.namespace("tuya_wifi_mcu")
TuyaWifiMcuNumber = tuya_wifi_mcu_ns.class_("TuyaWifiMcuNumber", number.Number, cg.Component)


def _validate_range(config):
    if config[CONF_MIN_VALUE] >= config[CONF_MAX_VALUE]:
        raise cv.Invalid("min_value must be less than max_value")
    return config


CONFIG_SCHEMA = cv.All(
    number.number_schema(TuyaWifiMcuNumber)
    .extend(
        {
            cv.GenerateID(CONF_TUYA_WIFI_MCU_ID): cv.use_id(TuyaWifiMcuComponent),
            cv.Required(CONF_DP_ID): cv.int_range(min=0, max=255),
            # Values as defined for the DP on the Tuya platform, in real units
            # (e.g. scale 1, range 0-1000 on Tuya -> min 0, max 100, step 0.1).
            cv.Required(CONF_MIN_VALUE): cv.float_,
            cv.Required(CONF_MAX_VALUE): cv.float_,
            cv.Optional(CONF_STEP, default=1): cv.positive_float,
            cv.Optional(CONF_SCALE, default=0): cv.int_range(min=0, max=9),
            cv.Optional(CONF_BIND_NUMBER_ID): cv.use_id(number.Number),
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    _validate_range,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_TUYA_WIFI_MCU_ID])
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await number.register_number(
        var,
        config,
        min_value=config[CONF_MIN_VALUE],
        max_value=config[CONF_MAX_VALUE],
        step=config[CONF_STEP],
    )
    cg.add(var.set_dp_id(config[CONF_DP_ID]))
    cg.add(var.set_scale(config[CONF_SCALE]))
    if CONF_BIND_NUMBER_ID in config:
        bind = await cg.get_variable(config[CONF_BIND_NUMBER_ID])
        cg.add(var.set_bind_number(bind))
    cg.add(parent.register_tuya_wifi_mcu_entity(var))
