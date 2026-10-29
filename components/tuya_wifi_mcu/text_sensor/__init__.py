import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_ID, CONF_KEY, CONF_TYPE, ENTITY_CATEGORY_DIAGNOSTIC

from .. import TuyaWifiMcuComponent

DEPENDENCIES = ["tuya_wifi_mcu"]

CONF_TUYA_WIFI_MCU_ID = "tuya_wifi_mcu_id"
CONF_DP_ID = "dp_id"
CONF_TEXT_SENSOR_ID = "text_sensor_id"
CONF_DAY = "day"

tuya_wifi_mcu_ns = cg.esphome_ns.namespace("tuya_wifi_mcu")
TuyaWifiMcuTextSensor = tuya_wifi_mcu_ns.class_(
    "TuyaWifiMcuTextSensor", text_sensor.TextSensor, cg.Component
)
TuyaWifiMcuServiceTextSensor = tuya_wifi_mcu_ns.class_(
    "TuyaWifiMcuServiceTextSensor", text_sensor.TextSensor, cg.PollingComponent
)
TuyaDpType = tuya_wifi_mcu_ns.enum("TuyaDpType", is_class=True)
ServiceType = tuya_wifi_mcu_ns.enum("TuyaServiceTextSensorType", is_class=True)

DP_TYPES = {"string": TuyaDpType.STRING, "raw": TuyaDpType.RAW}
SERVICE_TYPES = {
    "network_status": ServiceType.NETWORK_STATUS,
    "mac_address": ServiceType.MAC_ADDRESS,
    "weather": ServiceType.WEATHER,
}

_PARENT = {cv.GenerateID(CONF_TUYA_WIFI_MCU_ID): cv.use_id(TuyaWifiMcuComponent)}

_DP_SCHEMA = (
    text_sensor.text_sensor_schema(TuyaWifiMcuTextSensor)
    .extend(
        {
            **_PARENT,
            cv.Required(CONF_DP_ID): cv.int_range(min=0, max=255),
            # Local text sensor uploaded to Tuya (hex text for raw DPs).
            cv.Optional(CONF_TEXT_SENSOR_ID): cv.use_id(text_sensor.TextSensor),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


def _service_schema(interval, extra=None, **kwargs):
    schema = dict(_PARENT)
    if extra:
        schema.update(extra)
    return (
        text_sensor.text_sensor_schema(TuyaWifiMcuServiceTextSensor, **kwargs)
        .extend(schema)
        .extend(cv.polling_component_schema(interval))
    )


CONFIG_SCHEMA = cv.typed_schema(
    {
        "string": _DP_SCHEMA,
        "raw": _DP_SCHEMA,
        "network_status": _service_schema("60s", entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
        "mac_address": _service_schema("never", entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
        "weather": _service_schema(
            "never",
            {
                cv.Required(CONF_KEY): cv.All(cv.string_strict, cv.Length(min=1, max=32)),
                cv.Optional(CONF_DAY, default=0): cv.int_range(min=0, max=6),
            },
        ),
    },
    key=CONF_TYPE,
    default_type="string",
    lower=True,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_TUYA_WIFI_MCU_ID])
    sensor_type = config[CONF_TYPE]
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await text_sensor.register_text_sensor(var, config)

    if sensor_type in DP_TYPES:
        cg.add(var.set_dp_id(config[CONF_DP_ID]))
        cg.add(var.set_dp_type(DP_TYPES[sensor_type]))
        if CONF_TEXT_SENSOR_ID in config:
            source = await cg.get_variable(config[CONF_TEXT_SENSOR_ID])
            cg.add(var.set_source_text_sensor(source))
        cg.add(parent.register_tuya_wifi_mcu_entity(var))
        return

    cg.add(var.set_parent(parent))
    cg.add(var.set_service_type(SERVICE_TYPES[sensor_type]))
    if sensor_type == "weather":
        cg.add(var.set_weather_key(config[CONF_KEY]))
        cg.add(var.set_weather_day(config[CONF_DAY]))
        cg.add(parent.add_weather_key(config[CONF_KEY]))
