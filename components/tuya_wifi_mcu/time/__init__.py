import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import time
from esphome.const import CONF_ID, CONF_SOURCE

from .. import TuyaWifiMcuComponent

DEPENDENCIES = ["tuya_wifi_mcu"]

CONF_TUYA_WIFI_MCU_ID = "tuya_wifi_mcu_id"

tuya_wifi_mcu_ns = cg.esphome_ns.namespace("tuya_wifi_mcu")
TuyaWifiMcuTime = tuya_wifi_mcu_ns.class_("TuyaWifiMcuTime", time.RealTimeClock)
TimeSource = TuyaWifiMcuTime.enum("Source")
TIME_SOURCES = {
    "gmt": TimeSource.SOURCE_GMT,
    "local": TimeSource.SOURCE_LOCAL,
}

CONFIG_SCHEMA = time.TIME_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(TuyaWifiMcuTime),
        cv.GenerateID(CONF_TUYA_WIFI_MCU_ID): cv.use_id(TuyaWifiMcuComponent),
        # gmt (0x0C) is independent of time zones; local (0x1C) needs this
        # clock's `timezone:` to match the location the device is paired in.
        cv.Optional(CONF_SOURCE, default="gmt"): cv.enum(TIME_SOURCES, lower=True),
    }
).extend(cv.polling_component_schema("1h"))


async def to_code(config):
    parent = await cg.get_variable(config[CONF_TUYA_WIFI_MCU_ID])
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await time.register_time(var, config)
    cg.add(var.set_parent(parent))
    cg.add(var.set_source(config[CONF_SOURCE]))
