import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_ID, CONF_OPTIONS

from .. import TuyaWifiMcuComponent

DEPENDENCIES = ["tuya_wifi_mcu"]

CONF_TUYA_WIFI_MCU_ID = "tuya_wifi_mcu_id"
CONF_DP_ID = "dp_id"
CONF_BIND_SELECT_ID = "bind_select_id"

tuya_wifi_mcu_ns = cg.esphome_ns.namespace("tuya_wifi_mcu")
TuyaWifiMcuSelect = tuya_wifi_mcu_ns.class_("TuyaWifiMcuSelect", select.Select, cg.Component)


def _unique_options(options):
    if len(set(options)) != len(options):
        raise cv.Invalid("Select options must be unique")
    if len(options) > 256:
        raise cv.Invalid("A Tuya enum has at most 256 values")
    return options


CONFIG_SCHEMA = (
    select.select_schema(TuyaWifiMcuSelect)
    .extend(
        {
            cv.GenerateID(CONF_TUYA_WIFI_MCU_ID): cv.use_id(TuyaWifiMcuComponent),
            cv.Required(CONF_DP_ID): cv.int_range(min=0, max=255),
            # Order = Tuya enum index (first option is 0), exactly as on the Tuya platform.
            cv.Required(CONF_OPTIONS): cv.All(
                cv.ensure_list(cv.string_strict), cv.Length(min=1), _unique_options
            ),
            cv.Optional(CONF_BIND_SELECT_ID): cv.use_id(select.Select),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_TUYA_WIFI_MCU_ID])
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await select.register_select(var, config, options=config[CONF_OPTIONS])
    for option in config[CONF_OPTIONS]:
        cg.add(var.add_tuya_option(option))
    cg.add(var.set_dp_id(config[CONF_DP_ID]))
    if CONF_BIND_SELECT_ID in config:
        bind = await cg.get_variable(config[CONF_BIND_SELECT_ID])
        cg.add(var.set_bind_select(bind))
    cg.add(parent.register_tuya_wifi_mcu_entity(var))
