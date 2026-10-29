import inspect
import logging
import re

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation, pins
from esphome.components import uart
from esphome.const import (
    CONF_BUILD_FLAGS,
    CONF_ESPHOME,
    CONF_ID,
    CONF_INVERTED,
    CONF_MODE,
    CONF_NUMBER,
    CONF_PLATFORMIO_OPTIONS,
    CONF_TIME_ID,
    CONF_TRIGGER_ID,
    CONF_VALUE,
)
from esphome.core import CORE

DEPENDENCIES = ["uart"]
MULTI_CONF = False

CONF_PRODUCT_ID = "product_id"
CONF_MCU_VERSION = "mcu_version"
CONF_LEGACY_MCU_VERSION = "mcu_verersion"
CONF_WIFI_CONTROL_MODE = "wifi_control_mode"
CONF_WIFI_RESET_PIN = "wifi_reset_pin"
CONF_WIFI_LED_PIN = "wifi_led_pin"
CONF_WIFI_BLE_LED_PIN = "wifi_ble_led_pin"
CONF_PAIRING_MODE = "pairing_mode"
CONF_PAIRING_METHOD = "pairing_method"
CONF_FEATURE_CONFIG = "feature_config"
CONF_RESET_NOTIFICATION = "reset_notification"
CONF_BATCH_REPORTS = "batch_reports"
CONF_WEATHER = "weather"
CONF_KEYS = "keys"
CONF_FORECAST_DAYS = "forecast_days"
CONF_TIME_FORMAT = "time_format"
CONF_DP_ID = "dp_id"
CONF_AUTO_ACKNOWLEDGE = "auto_acknowledge"
CONF_ON_DP_DOWNLOAD = "on_dp_download"
CONF_ON_NETWORK_STATUS = "on_network_status"
CONF_ON_WEATHER = "on_weather"
CONF_ON_MODULE_RESET = "on_module_reset"
CONF_ON_UPLOAD_RESULT = "on_upload_result"
CONF_ON_WIFI_TEST = "on_wifi_test"
CONF_ON_MODULE_READY = "on_module_ready"
CONF_RECORD_TIME = "record_time"
CONF_SCALE = "scale"
CONF_WIDTH = "width"

_LOGGER = logging.getLogger(__name__)
_LEGACY_WIFI_CONTROL_FLAG = re.compile(
    r"(?<!\S)-D\s*WIFI_CONTROL_SELF_MODE(?:\s*=\s*([01]))?(?=\s|$)"
)


tuya_wifi_mcu_ns = cg.esphome_ns.namespace("tuya_wifi_mcu")
TuyaWifiMcuComponent = tuya_wifi_mcu_ns.class_(
    "TuyaWifiMcuComponent", cg.PollingComponent, uart.UARTDevice
)
WifiControlMode = tuya_wifi_mcu_ns.enum("WifiControlMode")

WIFI_CONTROL_MODES = {
    "mcu": WifiControlMode.WIFI_CONTROL_MODE_MCU,
    "module": WifiControlMode.WIFI_CONTROL_MODE_MODULE,
}

TuyaPairingMode = tuya_wifi_mcu_ns.enum("TuyaPairingMode")
PAIRING_MODES = {
    "ez": TuyaPairingMode.TUYA_PAIRING_EZ,
    "ap": TuyaPairingMode.TUYA_PAIRING_AP,
}
TuyaRecordTime = tuya_wifi_mcu_ns.enum("TuyaRecordTime", is_class=True)
RECORD_TIMES = {
    "module": TuyaRecordTime.MODULE,
    "local": TuyaRecordTime.LOCAL,
    "gmt": TuyaRecordTime.GMT,
}
TuyaUploadMode = tuya_wifi_mcu_ns.enum("TuyaUploadMode", is_class=True)
UPLOAD_MODES = {
    "async": TuyaUploadMode.ASYNC,
    "sync": TuyaUploadMode.SYNC,
    "record": TuyaUploadMode.RECORD,
}

# Triggers
DpDownloadTrigger = tuya_wifi_mcu_ns.class_(
    "DpDownloadTrigger",
    automation.Trigger.template(cg.uint8, cg.uint8, cg.std_vector.template(cg.uint8)),
)
NetworkStatusTrigger = tuya_wifi_mcu_ns.class_(
    "NetworkStatusTrigger", automation.Trigger.template(cg.uint8)
)
WeatherTrigger = tuya_wifi_mcu_ns.class_(
    "WeatherTrigger", automation.Trigger.template(cg.std_string, cg.uint8, cg.std_string)
)
ModuleResetTrigger = tuya_wifi_mcu_ns.class_(
    "ModuleResetTrigger", automation.Trigger.template(cg.uint8)
)
UploadResultTrigger = tuya_wifi_mcu_ns.class_(
    "UploadResultTrigger", automation.Trigger.template(cg.bool_, cg.bool_)
)
WifiTestTrigger = tuya_wifi_mcu_ns.class_(
    "WifiTestTrigger", automation.Trigger.template(cg.bool_, cg.uint8)
)
ModuleReadyTrigger = tuya_wifi_mcu_ns.class_(
    "ModuleReadyTrigger", automation.Trigger.template()
)

# Actions / conditions
UploadBoolAction = tuya_wifi_mcu_ns.class_("UploadBoolAction", automation.Action)
UploadValueAction = tuya_wifi_mcu_ns.class_("UploadValueAction", automation.Action)
UploadEnumAction = tuya_wifi_mcu_ns.class_("UploadEnumAction", automation.Action)
UploadBitmapAction = tuya_wifi_mcu_ns.class_("UploadBitmapAction", automation.Action)
UploadStringAction = tuya_wifi_mcu_ns.class_("UploadStringAction", automation.Action)
UploadRawAction = tuya_wifi_mcu_ns.class_("UploadRawAction", automation.Action)
ResetWifiAction = tuya_wifi_mcu_ns.class_("ResetWifiAction", automation.Action)
StartPairingAction = tuya_wifi_mcu_ns.class_("StartPairingAction", automation.Action)
WifiTestAction = tuya_wifi_mcu_ns.class_("WifiTestAction", automation.Action)
RequestWeatherAction = tuya_wifi_mcu_ns.class_("RequestWeatherAction", automation.Action)
ReportAllAction = tuya_wifi_mcu_ns.class_("ReportAllAction", automation.Action)
QueryNetworkStatusAction = tuya_wifi_mcu_ns.class_(
    "QueryNetworkStatusAction", automation.Action
)
CloudConnectedCondition = tuya_wifi_mcu_ns.class_(
    "CloudConnectedCondition", automation.Condition
)
ModuleOnlineCondition = tuya_wifi_mcu_ns.class_(
    "ModuleOnlineCondition", automation.Condition
)

# ESPHome 2026.x requires register_action(..., synchronous=...); older releases
# do not accept the keyword. Detect it once so both generations work.
_ACTION_KWARGS = (
    {"synchronous": True}
    if "synchronous" in inspect.signature(automation.register_action).parameters
    else {}
)


def _as_flag_strings(value):
    if isinstance(value, str):
        return [value]
    if isinstance(value, (list, tuple)):
        return [item for item in value if isinstance(item, str)]
    return []


def _legacy_wifi_control_mode():
    raw_config = getattr(CORE, "raw_config", None) or {}
    esphome_config = raw_config.get(CONF_ESPHOME) or {}
    values = _as_flag_strings(esphome_config.get(CONF_BUILD_FLAGS))

    platformio_options = esphome_config.get(CONF_PLATFORMIO_OPTIONS) or {}
    if isinstance(platformio_options, dict):
        for key, value in platformio_options.items():
            if key == CONF_BUILD_FLAGS or str(key).endswith(".extra_flags"):
                values.extend(_as_flag_strings(value))

    modes = set()
    for value in values:
        for match in _LEGACY_WIFI_CONTROL_FLAG.finditer(value):
            modes.add("module" if match.group(1) in (None, "1") else "mcu")

    if len(modes) > 1:
        raise cv.Invalid("Conflicting WIFI_CONTROL_SELF_MODE build flags")
    return next(iter(modes), None)


def _normalize_wifi_control_config(config):
    config = config.copy()
    legacy_mode = _legacy_wifi_control_mode()
    configured_mode = config.get(CONF_WIFI_CONTROL_MODE)
    if configured_mode is not None:
        configured_mode = cv.string_strict(configured_mode).lower()

    if legacy_mode is not None:
        _LOGGER.warning(
            "WIFI_CONTROL_SELF_MODE is deprecated; remove the build flag and use wifi_control_mode: %s",
            legacy_mode,
        )
        if configured_mode is not None and configured_mode != legacy_mode:
            raise cv.Invalid(
                "wifi_control_mode conflicts with the deprecated WIFI_CONTROL_SELF_MODE build flag"
            )
        config.setdefault(CONF_WIFI_CONTROL_MODE, legacy_mode)

    effective_mode = configured_mode or legacy_mode or "mcu"
    if effective_mode == "mcu":
        for key in (CONF_WIFI_RESET_PIN, CONF_WIFI_LED_PIN):
            value = config.get(key)
            try:
                number = cv.int_range(min=0, max=99)(value)
            except cv.Invalid:
                continue
            if number == 0:
                config.pop(key)
    return config


def _normalize_reset_pin(value):
    if isinstance(value, dict):
        value = value.copy()
        value.setdefault(CONF_INVERTED, True)
        return value
    return {CONF_NUMBER: value, CONF_INVERTED: True}


_reset_pin_schema = cv.All(_normalize_reset_pin, pins.gpio_input_pin_schema)


def _normalize_mcu_version(config):
    config = config.copy()
    if CONF_MCU_VERSION in config and CONF_LEGACY_MCU_VERSION in config:
        raise cv.Invalid(
            f"Specify only one of {CONF_MCU_VERSION} or {CONF_LEGACY_MCU_VERSION}"
        )
    if CONF_LEGACY_MCU_VERSION in config:
        config[CONF_MCU_VERSION] = config.pop(CONF_LEGACY_MCU_VERSION)
    config.setdefault(CONF_MCU_VERSION, "1.0.0")
    return config


def _weather_key(value):
    value = cv.string_strict(value)
    if not value or len(value) > 32:
        raise cv.Invalid("Weather parameter names must be 1-32 characters")
    return value


WEATHER_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_KEYS, default=[]): cv.ensure_list(_weather_key),
        cv.Optional(CONF_FORECAST_DAYS, default=1): cv.int_range(min=1, max=7),
        cv.Optional(CONF_TIME_FORMAT, default="local"): cv.one_of("local", "unix", lower=True),
    }
)

COMMON_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(TuyaWifiMcuComponent),
        cv.Required(CONF_PRODUCT_ID): cv.string,
        cv.Optional(CONF_MCU_VERSION): cv.string,
        cv.Optional(CONF_LEGACY_MCU_VERSION): cv.string,
        # ---- Extended options (all optional; defaults reproduce v1.4.0) ----
        cv.Optional(CONF_PAIRING_MODE, default=0): cv.int_range(min=0, max=5),
        cv.Optional(CONF_PAIRING_METHOD): cv.one_of("ez_ap", "ap", lower=True),
        cv.Optional(CONF_FEATURE_CONFIG): cv.string_strict,
        cv.Optional(CONF_RESET_NOTIFICATION, default=False): cv.boolean,
        cv.Optional(CONF_BATCH_REPORTS, default=False): cv.boolean,
        cv.Optional(CONF_WEATHER): WEATHER_SCHEMA,
        cv.Optional(CONF_ON_DP_DOWNLOAD): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(DpDownloadTrigger),
                cv.Optional(CONF_DP_ID): cv.int_range(min=0, max=255),
                cv.Optional(CONF_AUTO_ACKNOWLEDGE, default=True): cv.boolean,
            }
        ),
        cv.Optional(CONF_ON_NETWORK_STATUS): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(NetworkStatusTrigger)}
        ),
        cv.Optional(CONF_ON_WEATHER): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(WeatherTrigger)}
        ),
        cv.Optional(CONF_ON_MODULE_RESET): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ModuleResetTrigger)}
        ),
        cv.Optional(CONF_ON_UPLOAD_RESULT): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(UploadResultTrigger)}
        ),
        cv.Optional(CONF_ON_WIFI_TEST): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(WifiTestTrigger)}
        ),
        cv.Optional(CONF_ON_MODULE_READY): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ModuleReadyTrigger)}
        ),
    }
).extend(cv.COMPONENT_SCHEMA).extend(uart.UART_DEVICE_SCHEMA)

CONFIG_SCHEMA = cv.All(
    _normalize_wifi_control_config,
    cv.typed_schema(
        {
            "mcu": COMMON_SCHEMA.extend(
                {
                    cv.Optional(CONF_WIFI_RESET_PIN): cv.Any(
                        cv.int_range(min=0, max=99), _reset_pin_schema
                    ),
                    cv.Optional(CONF_WIFI_LED_PIN): cv.Any(
                        cv.int_range(min=0, max=99), pins.gpio_output_pin_schema
                    ),
                }
            ),
            "module": COMMON_SCHEMA.extend(
                {
                    cv.Optional(CONF_WIFI_RESET_PIN, default=0): cv.int_range(min=0, max=255),
                    cv.Optional(CONF_WIFI_LED_PIN, default=0): cv.int_range(min=0, max=255),
                    # Third work-mode byte (BLE status LED of WiFi+BLE combo modules).
                    cv.Optional(CONF_WIFI_BLE_LED_PIN): cv.int_range(min=0, max=255),
                }
            ),
        },
        key=CONF_WIFI_CONTROL_MODE,
        default_type="mcu",
        enum=WIFI_CONTROL_MODES,
        lower=True,
    ),
    _normalize_mcu_version,
)


async def to_code(config):
    uart_component = await cg.get_variable(config["uart_id"])
    var = cg.new_Pvariable(config[CONF_ID], uart_component)

    cg.add(var.set_product_id(config[CONF_PRODUCT_ID]))
    cg.add(var.set_version(config[CONF_MCU_VERSION]))
    cg.add(var.set_wifi_control_mode(config[CONF_WIFI_CONTROL_MODE].enum_value))

    if config[CONF_WIFI_CONTROL_MODE] == "mcu":
        if CONF_WIFI_RESET_PIN in config:
            if isinstance(config[CONF_WIFI_RESET_PIN], int):
                cg.add(var.set_legacy_wifi_reset_pin(int(config[CONF_WIFI_RESET_PIN])))
            else:
                reset_pin = await cg.gpio_pin_expression(config[CONF_WIFI_RESET_PIN])
                cg.add(var.set_wifi_reset_pin(reset_pin))
        if CONF_WIFI_LED_PIN in config:
            if isinstance(config[CONF_WIFI_LED_PIN], int):
                cg.add(var.set_legacy_wifi_led_pin(int(config[CONF_WIFI_LED_PIN])))
            else:
                led_pin = await cg.gpio_pin_expression(config[CONF_WIFI_LED_PIN])
                cg.add(var.set_wifi_led_pin(led_pin))
    else:
        cg.add(var.set_module_wifi_reset_pin(config[CONF_WIFI_RESET_PIN]))
        cg.add(var.set_module_wifi_led_pin(config[CONF_WIFI_LED_PIN]))
        if CONF_WIFI_BLE_LED_PIN in config:
            cg.add(var.set_module_ble_led_pin(config[CONF_WIFI_BLE_LED_PIN]))

    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_pairing_mode(config[CONF_PAIRING_MODE]))
    if CONF_PAIRING_METHOD in config:
        cg.add(var.set_pairing_method(0 if config[CONF_PAIRING_METHOD] == "ez_ap" else 1))
    if CONF_FEATURE_CONFIG in config:
        cg.add(var.set_feature_config(config[CONF_FEATURE_CONFIG]))
    if config[CONF_RESET_NOTIFICATION]:
        cg.add(var.set_reset_notification(True))
    if config[CONF_BATCH_REPORTS]:
        cg.add(var.set_batch_reports(True))
    if CONF_WEATHER in config:
        weather = config[CONF_WEATHER]
        for key in weather[CONF_KEYS]:
            cg.add(var.add_weather_key(key))
        cg.add(var.set_weather_forecast_days(weather[CONF_FORECAST_DAYS]))
        cg.add(var.set_weather_time_key(f"t.{weather[CONF_TIME_FORMAT]}"))

    for conf in config.get(CONF_ON_DP_DOWNLOAD, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        if CONF_DP_ID in conf:
            cg.add(trigger.set_dp_id(conf[CONF_DP_ID]))
        cg.add(trigger.set_auto_acknowledge(conf[CONF_AUTO_ACKNOWLEDGE]))
        await automation.build_automation(
            trigger,
            [(cg.uint8, "dp_id"), (cg.uint8, "type"), (cg.std_vector.template(cg.uint8), "data")],
            conf,
        )
    for conf in config.get(CONF_ON_NETWORK_STATUS, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.uint8, "status")], conf)
    for conf in config.get(CONF_ON_WEATHER, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger, [(cg.std_string, "key"), (cg.uint8, "day"), (cg.std_string, "value")], conf
        )
    for conf in config.get(CONF_ON_MODULE_RESET, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.uint8, "reason")], conf)
    for conf in config.get(CONF_ON_UPLOAD_RESULT, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger, [(cg.bool_, "success"), (cg.bool_, "record")], conf
        )
    for conf in config.get(CONF_ON_WIFI_TEST, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger, [(cg.bool_, "success"), (cg.uint8, "strength")], conf
        )
    for conf in config.get(CONF_ON_MODULE_READY, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)


# ---------------------------------------------------------------------------
# Actions
# ---------------------------------------------------------------------------


def _hex_or_bytes(value):
    """RAW payload: a list of byte values or a hex string ("0a1bff", "0A:1B")."""
    if isinstance(value, list):
        return [cv.hex_uint8_t(item) for item in value]
    text = cv.string_strict(value).replace(" ", "").replace(":", "").replace("-", "")
    if len(text) % 2:
        raise cv.Invalid("Hex string must have an even number of digits")
    try:
        return list(bytes.fromhex(text))
    except ValueError as err:
        raise cv.Invalid(f"Invalid hex string: {err}") from err


def _upload_schema(value_validator, extra=None):
    schema = {
        cv.GenerateID(): cv.use_id(TuyaWifiMcuComponent),
        cv.Required(CONF_DP_ID): cv.int_range(min=0, max=255),
        cv.Required(CONF_VALUE): cv.templatable(value_validator),
        cv.Optional(CONF_MODE, default="async"): cv.enum(UPLOAD_MODES, lower=True),
        cv.Optional(CONF_RECORD_TIME, default="module"): cv.enum(RECORD_TIMES, lower=True),
        cv.Optional(CONF_TIME_ID): cv.use_id(cg.esphome_ns.namespace("time").class_("RealTimeClock")),
    }
    if extra:
        schema.update(extra)
    return cv.Schema(schema)


async def _upload_to_code(config, action_id, template_arg, args, value_type):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_dp_id(config[CONF_DP_ID]))
    cg.add(var.set_mode(config[CONF_MODE]))
    cg.add(var.set_record_time(config[CONF_RECORD_TIME]))
    if CONF_TIME_ID in config:
        clock = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time(clock))
    value = config[CONF_VALUE]
    if value_type is None:
        # RAW: constant byte lists become a std::vector initializer.
        if cg.is_template(value):
            templ = await cg.templatable(value, args, cg.std_vector.template(cg.uint8))
        else:
            templ = await cg.templatable(
                cg.RawExpression(
                    "std::vector<uint8_t>{" + ", ".join(f"0x{b:02X}" for b in value) + "}"
                ),
                args,
                cg.std_vector.template(cg.uint8),
            )
    else:
        templ = await cg.templatable(value, args, value_type)
    cg.add(var.set_value(templ))
    return var


@automation.register_action(
    "tuya_wifi_mcu.upload_bool", UploadBoolAction, _upload_schema(cv.boolean), **_ACTION_KWARGS
)
async def upload_bool_to_code(config, action_id, template_arg, args):
    return await _upload_to_code(config, action_id, template_arg, args, cg.bool_)


@automation.register_action(
    "tuya_wifi_mcu.upload_value",
    UploadValueAction,
    _upload_schema(cv.float_, {cv.Optional(CONF_SCALE, default=0): cv.int_range(min=0, max=9)}),
    **_ACTION_KWARGS,
)
async def upload_value_to_code(config, action_id, template_arg, args):
    var = await _upload_to_code(config, action_id, template_arg, args, cg.float_)
    cg.add(var.set_scale(config[CONF_SCALE]))
    return var


@automation.register_action(
    "tuya_wifi_mcu.upload_enum",
    UploadEnumAction,
    _upload_schema(cv.int_range(min=0, max=255)),
    **_ACTION_KWARGS,
)
async def upload_enum_to_code(config, action_id, template_arg, args):
    return await _upload_to_code(config, action_id, template_arg, args, cg.uint8)


@automation.register_action(
    "tuya_wifi_mcu.upload_bitmap",
    UploadBitmapAction,
    _upload_schema(
        cv.int_range(min=0, max=0xFFFFFFFF),
        {cv.Optional(CONF_WIDTH, default=4): cv.one_of(1, 2, 4, int=True)},
    ),
    **_ACTION_KWARGS,
)
async def upload_bitmap_to_code(config, action_id, template_arg, args):
    var = await _upload_to_code(config, action_id, template_arg, args, cg.uint32)
    cg.add(var.set_width(config[CONF_WIDTH]))
    return var


@automation.register_action(
    "tuya_wifi_mcu.upload_string",
    UploadStringAction,
    _upload_schema(cv.string),
    **_ACTION_KWARGS,
)
async def upload_string_to_code(config, action_id, template_arg, args):
    return await _upload_to_code(config, action_id, template_arg, args, cg.std_string)


@automation.register_action(
    "tuya_wifi_mcu.upload_raw",
    UploadRawAction,
    _upload_schema(_hex_or_bytes),
    **_ACTION_KWARGS,
)
async def upload_raw_to_code(config, action_id, template_arg, args):
    return await _upload_to_code(config, action_id, template_arg, args, None)


_SIMPLE_ID_SCHEMA = automation.maybe_simple_id(
    {cv.GenerateID(): cv.use_id(TuyaWifiMcuComponent)}
)


async def _simple_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


for _name, _cls in (
    ("tuya_wifi_mcu.reset_wifi", ResetWifiAction),
    ("tuya_wifi_mcu.wifi_test", WifiTestAction),
    ("tuya_wifi_mcu.request_weather", RequestWeatherAction),
    ("tuya_wifi_mcu.report_all", ReportAllAction),
    ("tuya_wifi_mcu.query_network_status", QueryNetworkStatusAction),
):
    automation.register_action(_name, _cls, _SIMPLE_ID_SCHEMA, **_ACTION_KWARGS)(
        _simple_action_to_code
    )


@automation.register_action(
    "tuya_wifi_mcu.start_pairing",
    StartPairingAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(TuyaWifiMcuComponent),
            cv.Optional(CONF_MODE, default="ez"): cv.enum(PAIRING_MODES, lower=True),
        }
    ),
    **_ACTION_KWARGS,
)
async def start_pairing_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_pairing_mode(config[CONF_MODE]))
    return var


@automation.register_condition(
    "tuya_wifi_mcu.is_cloud_connected", CloudConnectedCondition, _SIMPLE_ID_SCHEMA
)
@automation.register_condition(
    "tuya_wifi_mcu.is_module_online", ModuleOnlineCondition, _SIMPLE_ID_SCHEMA
)
async def condition_to_code(config, condition_id, template_arg, args):
    var = cg.new_Pvariable(condition_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
