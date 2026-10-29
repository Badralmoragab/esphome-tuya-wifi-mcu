import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    CONF_KEY,
    CONF_SENSOR_ID,
    CONF_TYPE,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_DECIBEL_MILLIWATT,
    UNIT_PERCENT,
)

from .. import TuyaWifiMcuComponent

DEPENDENCIES = ["tuya_wifi_mcu"]

CONF_TUYA_WIFI_MCU_ID = "tuya_wifi_mcu_id"
CONF_DP_ID = "dp_id"
CONF_SCALE = "scale"
CONF_BITMAP_WIDTH = "bitmap_width"
CONF_REPORT_DELTA = "report_delta"
CONF_DAY = "day"

tuya_wifi_mcu_ns = cg.esphome_ns.namespace("tuya_wifi_mcu")
TuyaWifiMcuSensor = tuya_wifi_mcu_ns.class_("TuyaWifiMcuSensor", sensor.Sensor, cg.Component)
TuyaWifiMcuServiceSensor = tuya_wifi_mcu_ns.class_(
    "TuyaWifiMcuServiceSensor", sensor.Sensor, cg.PollingComponent
)
TuyaDpType = tuya_wifi_mcu_ns.enum("TuyaDpType", is_class=True)
ServiceType = tuya_wifi_mcu_ns.enum("TuyaServiceSensorType", is_class=True)

DP_TYPES = {
    "value": TuyaDpType.VALUE,
    "enum": TuyaDpType.ENUM,
    "bitmap": TuyaDpType.BITMAP,
    "bool": TuyaDpType.BOOLEAN,
}
SERVICE_TYPES = {
    "wifi_rssi": ServiceType.WIFI_RSSI,
    "free_memory": ServiceType.FREE_MEMORY,
    "wifi_test": ServiceType.WIFI_TEST,
    "weather": ServiceType.WEATHER,
}

_PARENT = {cv.GenerateID(CONF_TUYA_WIFI_MCU_ID): cv.use_id(TuyaWifiMcuComponent)}


def _dp_schema(extra=None):
    schema = {
        **_PARENT,
        cv.Required(CONF_DP_ID): cv.int_range(min=0, max=255),
        # Local sensor uploaded to Tuya (SHT30, ADS1115, energy meter...).
        cv.Optional(CONF_SENSOR_ID): cv.use_id(sensor.Sensor),
        # Minimum change (in real units) before a new upload; 0 = every change.
        cv.Optional(CONF_REPORT_DELTA, default=0.0): cv.positive_float,
    }
    if extra:
        schema.update(extra)
    return sensor.sensor_schema(TuyaWifiMcuSensor).extend(schema).extend(cv.COMPONENT_SCHEMA)


def _service_schema(interval, extra=None, **kwargs):
    schema = dict(_PARENT)
    if extra:
        schema.update(extra)
    return (
        sensor.sensor_schema(TuyaWifiMcuServiceSensor, **kwargs)
        .extend(schema)
        .extend(cv.polling_component_schema(interval))
    )


_WEATHER_EXTRA = {
    # Parameter name as in the Tuya weather service, e.g. w.temp, w.humidity, w.pm25.
    cv.Required(CONF_KEY): cv.All(cv.string_strict, cv.Length(min=1, max=32)),
    cv.Optional(CONF_DAY, default=0): cv.int_range(min=0, max=6),
}

# `type` selects either a DP mirror (default "value", so plain DP sensors need
# no type) or a module service. typed_schema keeps each variant's keys strict.
CONFIG_SCHEMA = cv.typed_schema(
    {
        "value": _dp_schema({cv.Optional(CONF_SCALE, default=0): cv.int_range(min=0, max=9)}),
        "enum": _dp_schema(),
        "bitmap": _dp_schema(
            {cv.Optional(CONF_BITMAP_WIDTH, default=4): cv.one_of(1, 2, 4, int=True)}
        ),
        "bool": _dp_schema(),
        "wifi_rssi": _service_schema(
            "60s",
            unit_of_measurement=UNIT_DECIBEL_MILLIWATT,
            device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            accuracy_decimals=0,
        ),
        "free_memory": _service_schema(
            "60s",
            unit_of_measurement="B",
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            accuracy_decimals=0,
        ),
        "wifi_test": _service_schema(
            "never",
            unit_of_measurement=UNIT_PERCENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            accuracy_decimals=0,
        ),
        "weather": _service_schema("never", _WEATHER_EXTRA),
    },
    key=CONF_TYPE,
    default_type="value",
    lower=True,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_TUYA_WIFI_MCU_ID])
    sensor_type = config[CONF_TYPE]
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await sensor.register_sensor(var, config)

    if sensor_type in DP_TYPES:
        cg.add(var.set_dp_id(config[CONF_DP_ID]))
        cg.add(var.set_dp_type(DP_TYPES[sensor_type]))
        cg.add(var.set_report_delta(config[CONF_REPORT_DELTA]))
        if CONF_SCALE in config:
            cg.add(var.set_scale(config[CONF_SCALE]))
        if CONF_BITMAP_WIDTH in config:
            cg.add(var.set_bitmap_width(config[CONF_BITMAP_WIDTH]))
        if CONF_SENSOR_ID in config:
            source = await cg.get_variable(config[CONF_SENSOR_ID])
            cg.add(var.set_source_sensor(source))
        cg.add(parent.register_tuya_wifi_mcu_entity(var))
        return

    cg.add(var.set_parent(parent))
    cg.add(var.set_service_type(SERVICE_TYPES[sensor_type]))
    if sensor_type == "weather":
        cg.add(var.set_weather_key(config[CONF_KEY]))
        cg.add(var.set_weather_day(config[CONF_DAY]))
        # Every weather entity subscribes its parameter on the module automatically.
        cg.add(parent.add_weather_key(config[CONF_KEY]))
