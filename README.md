# ESPHome Tuya WiFi MCU

ESPHome external component for ESP32 devices that act as the **MCU side of the Tuya MCU serial protocol**, talking to a Tuya WiFi (or WiFi+BLE) module over UART. It is generic: relay boards, dimmers, sensors, thermostats or any other product defined on the Tuya IoT platform can be described entirely in YAML.

The component supports both ESPHome frameworks:

- Arduino
- ESP-IDF

The Tuya serial protocol implementation is included in this component and no longer depends on the Arduino-only `Tuya_WiFi_MCU_SDK` library.

## What's new in v2.0.0

Everything below is additive: every v1.4.0 configuration keeps working unchanged (verified by the original test suite, which passes on ESPHome 2026.10.1).

| Area | v2.0.0 support |
| --- | --- |
| DP types | bool (`switch`, `binary_sensor`), value (`number`, `sensor`, `light`), enum (`select`, `sensor`), string (`text`, `text_sensor`), raw (`text_sensor` as hex), bitmap (`sensor`) |
| Any other DP | `on_dp_download` trigger + `tuya_wifi_mcu.upload_*` actions for all six types |
| Upload modes | async (`0x07`), sync with result (`0x22`/`0x23`), record-type with time stamp (`0x34 0x0B`) |
| Time | `time:` platform from the module: GMT (`0x0C`) or local time (`0x1C`) |
| Network | status (`0x03` push + `0x2B` query), WiFi RSSI (`0x24`), MAC (`0x2D`), module free memory (`0x0F`) |
| Production test | WiFi scan test (`0x0E`) |
| Weather | weather service (`0x20`/`0x21`, `0x34 0x03`) as sensors/text sensors |
| Module control | reset WiFi (`0x04`), start pairing EZ/AP (`0x05`), reset notifications (`0x34 0x04/0x05`), feature configuration (`0x37`), pairing mode/method in product info, 3-byte work mode (BLE LED) |
| Scalability | aggregated state reports (`batch_reports`), 1024-byte frames both directions |
| ESPHome | tested with 2026.10.1 (host e2e + ESP32-S3 ESP-IDF build) |

Not implemented: MCU firmware OTA through the Tuya cloud (`0x0A`/`0x0B`); use ESPHome OTA.

## Basic configuration

```yaml
esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: esp-idf  # or arduino

external_components:
  - source:
      type: git
      url: https://github.com/Badralmoragab/esphome-tuya-wifi-mcu
      ref: v2.0.0

uart:
  id: tuya_mcu_uart
  tx_pin: GPIO33
  rx_pin: GPIO14
  baud_rate: 9600
  rx_buffer_size: 1024

tuya_wifi_mcu:
  product_id: "abcdefghijklmnop"
  mcu_version: "1.0.0"
  uart_id: tuya_mcu_uart
  wifi_control_mode: mcu
  wifi_reset_pin:
    number: GPIO5
    mode: INPUT_PULLUP
  wifi_led_pin: GPIO12
```

## WiFi control modes

### `wifi_control_mode: mcu`

ESPHome handles the reset button and WiFi status LED. This is the default and is equivalent to the previous `WIFI_CONTROL_SELF_MODE=0` build flag.

- Both pins are optional. The reset button is active-low by default.
- Legacy scalar pin values retain the original integer conversion and range 0–99. Nonzero values use the legacy GPIO path without ESPHome pin reservation or duplicate-use validation; existing shared-pin configurations remain accepted. This does not make every number a usable GPIO on every board.
- A scalar value that converts to zero disables the pin. This includes `0`, numeric strings such as `"0"` and `"0x0"`, `false`, and `0.0`.
- Explicit GPIO names and dictionaries use ESPHome's pin schemas and validation. Use `GPIO0` or `{number: GPIO0}` for the actual ESP32 GPIO0 pin, not the disabled-pin sentinel. A reset-pin dictionary defaults to `inverted: true`; set `inverted: false` for an active-high button. The LED uses an output-pin schema.

### `wifi_control_mode: module`

The Tuya WiFi module handles its own reset button and status LED. This is equivalent to the previous `WIFI_CONTROL_SELF_MODE=1` build flag.

In this mode, `wifi_reset_pin` and `wifi_led_pin` are optional numeric pin identifiers for the Tuya module, not ESP32 GPIOs. Each defaults to `0`, preserving the old behavior when both are omitted. Numeric pin `0` is a valid module pin, not a disable sentinel. The work-mode response sends the LED identifier first, followed by the reset identifier.

```yaml
tuya_wifi_mcu:
  product_id: "abcdefghijklmnop"
  uart_id: tuya_mcu_uart
  wifi_control_mode: module
  wifi_reset_pin: 1
  wifi_led_pin: 2
```

## Entities

All Tuya entities require an integer `dp_id`. As in the original component, any integer is accepted and its low eight bits are used on the wire: `-1` becomes `255`, `256` becomes `0`, and `257` becomes `1`. DP ID `0` is supported. Optional bind IDs synchronize the Tuya entity with another local ESPHome entity. Duplicate wire IDs remain supported: each downloaded record is sent to every entity with the same wire ID and DP type, with one acknowledgement per record.

```yaml
switch:
  - platform: gpio
    id: relay_1
    pin: GPIO4

  - platform: tuya_wifi_mcu
    id: relay_1_tuya
    dp_id: 1
    bind_switch_id: relay_1
    internal: true

binary_sensor:
  - platform: template
    id: input_1
    lambda: return false;

  - platform: tuya_wifi_mcu
    id: input_1_tuya
    dp_id: 110
    bind_binary_sensor_id: input_1
    internal: true
```

Brightness lights use a Tuya `value` DP encoded as a four-byte big-endian value. Normal brightness maps to 0–100. Local brightness-to-percentage conversion truncates rather than rounds; remote integer percentages are retained exactly instead of being recomputed from floating-point light state. For compatibility, downloads above 100 are also accepted: physical brightness is clamped to `1.0`, while the stored and reported Tuya value retains the low eight bits of the download (`101` stays `101`, `256` becomes `0`, and `300` becomes `44`). A later local light command replaces that stored value with the local percentage. This preserves existing behavior; it is not a recommendation to send out-of-range brightness values.

```yaml
output:
  - platform: ledc
    id: dimmer_output
    pin: GPIO18

light:
  - platform: monochromatic
    id: local_dimmer
    output: dimmer_output

  - platform: tuya_wifi_mcu
    id: tuya_dimmer
    output: dimmer_output
    bind_light_id: local_dimmer
    dp_id: 173
    internal: true
```

## Compatibility with existing configurations

Existing component and entity YAML does not require migration to use Arduino or ESP-IDF:

- `WIFI_CONTROL_SELF_MODE=0` and `WIFI_CONTROL_SELF_MODE=1` remain supported and select MCU or module mode respectively. They emit a deprecation warning; switching to `wifi_control_mode` is optional. If both forms are specified, they must agree.
- Legacy scalar pins and zero-disable values remain accepted as described above. Omitting the pins in module mode preserves the old `0`/`0` defaults.
- The original misspelled `mcu_verersion` remains supported. `mcu_version` is an optional corrected spelling; use one spelling, not both. The default is still `"1.0.0"`.
- Product IDs and versions retain ESPHome's original string coercion, including numeric inputs and empty strings. There is no schema length or version-format restriction. Runtime storage safely truncates the product ID to 16 bytes and the version to 5 bytes; shorter values are stored as supplied. These are byte limits, not Unicode character limits, and should still match the Tuya product definition.
- No external Tuya Arduino SDK is required. Its uninitialized product-info storage and erroneous VALUE-download acknowledgement are not reproduced; compatibility does not require copying those SDK defects.

The protocol parser has a bounded 1024-byte payload capacity. Set the UART `rx_buffer_size` to at least 1024 when the Tuya product may aggregate many DP records into one download frame.

## Verification

With Docker installed, the Makefile pins the official `ghcr.io/esphome/esphome:2026.10.1` container for reproducible local validation:

```bash
make host-test
make e2e-test
make config-test
make config-all
make compile-all
# Or run the complete matrix:
make ci
```

Override `ESPHOME_VERSION` or `ESPHOME_IMAGE` only when deliberately testing another ESPHome release. CI is configured to validate and compile seven fixtures (the five below plus `esp-idf-extended` and `arduino-extended`) with the same pinned version:

- Arduino + MCU-managed WiFi controls
- Arduino + Tuya-module controls
- ESP-IDF + MCU-managed WiFi controls
- ESP-IDF + Tuya-module controls
- `legacy-main`: the original main-branch Arduino example, unchanged except for the local external-component path

`make config-test` uses standard-library `unittest` discovery to run 22 tests across both configuration test files. The legacy regressions load the original fixture through ESPHome's real configuration reader, check old scalar and string coercions, exercise complete configurations with shared pins and wrapped DP IDs, and verify all three entity code generators use the low eight DP-ID bits. They also distinguish the permissive legacy scalar pin `99` from an explicit GPIO dictionary with `number: 99`, which still fails ESPHome GPIO validation. These Python configuration tests require neither a firmware build nor the old SDK.

The framework-independent `make host-test` tests cover framing, checksums, fragmentation, explicit idle timeout recovery, bounded oversized-frame skipping, duplicate-DP dispatch, brightness conversion, Boolean DP frame encoding, and four-byte value encoding.

### ESPHome host end-to-end tests

`make e2e-test` compiles three fixtures with the real ESPHome `host` platform and is configured to run 28 Python `unittest` cases:

- `tests/e2e/host.yaml`: 13 scenarios in `HostE2ETest`.
- `tests/e2e/host-legacy.yaml`: the same 13 inherited scenarios in `LegacyHostE2ETest`.
- `tests/e2e/host-filtered.yaml`: two binary-sensor filter scenarios in `FilteredHostE2ETest`.

All three use the shared `HostFirmwareTest` harness. Each case starts a fresh native firmware process and connects it to a simulated Tuya module through a POSIX pseudo-terminal (PTY). An `aioesphomeapi` client controls and subscribes to the real ESPHome entities over the native API; no ESPHome runtime or component is mocked.

The legacy fixture reuses the base fixture through `packages` and exercises the original configuration forms end to end:

- `WIFI_CONTROL_SELF_MODE=1` selects module mode, with omitted pins reported as LED `0`, reset `0`.
- Numeric `product_id: 123456789012345678901` is converted to a string and truncated to `"1234567890123456"` on the wire.
- The legacy `mcu_verersion: "1.2.34567"` spelling produces the five-byte version `"1.2.3"`.
- Switch DP `256`, binary-sensor DP `-1`, and light DP `259` use wire IDs `0`, `255`, and `3` respectively.

The base and legacy fixtures run the same scenarios covering:

- Heartbeat, product/work-mode queries, WiFi-state acknowledgement, and full DP state queries.
- Separate first-download `false` and `true` cases starting with both binary sensors uninitialized. Each verifies the Tuya sensor and its bound sensor acquire the requested state and produce exactly one acknowledgement.
- Multi-DP downloads updating bound switches, binary sensors, lights, and observable output levels.
- Local switch/input changes uploading DPs, plus switch control through the Tuya entity.
- Local and Tuya light transitions reporting logical brightness rather than gamma-corrected output or intermediate transition values. Percentage conversion truncates rather than rounds: `0.429` reports `42`, and `0.005` reports `0`.
- Every integer brightness download from `0` through `100` retaining its exact reported value, including `53` and `59`; floating-point light-state updates must not turn these into `52` and `58`.
- Legacy brightness downloads `101`, `255`, `256`, `300`, and `UINT32_MAX`: the stored and reported state retains the low byte while the output is clamped to `1.0`. Subsequent local 100% commands through either the Tuya light or its bound light restore a reported state of `100`.
- Repeated-download acknowledgements without duplicate/feedback-loop uploads.
- Bad checksums, unknown IDs, wrong DP types, invalid Boolean values, and malformed aggregate downloads.
- Fragmented UART frames, idle timeout recovery, and oversized frames containing embedded commands.

The filtered fixture also reuses the base through `packages`, adding `delayed_on: 1s` to `tuya_input`. Its two scenarios verify that a first `true` download delays the first publication on both the Tuya sensor and its bound sensor, and that later `true` downloads are delayed and can be cancelled by a following `false` download. Accepted Boolean downloads receive an immediate acknowledgement echoing the input, preserving the old SDK's valid behavior; this acknowledgement is not a claim that a filter has already published the value. State queries report the actual post-filter state, and a delayed publication generates a separate state report. Binary-sensor synchronization uses full-state callbacks so the first valid publication is propagated without bypassing filters.

The Python runner and firmware execute in the same pinned Docker container, so no board, serial device mapping, Home Assistant instance, exposed port, or additional Python packages are required. The first build downloads the PlatformIO native platform; later builds reuse `.cache/` and `tests/e2e/.esphome/`. For an existing local Linux ESPHome environment with a C++ compiler, use `make e2e-test PYTHON=python3` instead.

The base and filtered fixtures use MCU control mode without GPIOs; the legacy fixture uses module mode with default pin identifiers. None of the fixtures preinitializes either binary sensor. Ordinary test setup initializes them with a first UART `false` download, while the dedicated first-download cases start from missing state. Periodic polling is suspended to keep unsolicited reports out of assertions; full-state reporting is exercised with protocol state queries. Physical GPIO behavior, the 60-second polling cadence, and the Tuya cloud are not covered by these host tests. The ESP32 compilation matrix remains separate and is not a substitute for hardware testing.

Every run uses a temporary UART symlink, an available API port, and isolated preference storage. API/UART waits and compilation have timeouts, and firmware processes are terminated even when a test fails. Firmware logs are saved under `tests/e2e/.esphome/logs/`; failed tests also print their firmware log. CI runs `make e2e-test` as a separate job and uploads these logs on failure. Both `make test` and `make ci` include the e2e suite.

---

# v2.0.0 reference

## How the pieces fit

```
Tuya app/cloud ⇄ Tuya WiFi module ⇄ UART ⇄ tuya_wifi_mcu (this component)
                                              ├─ DP entities  (switch, light, number, select, text, sensor, ...)
                                              ├─ on_dp_download + upload_* actions (any DP, any type)
                                              └─ module services (time, network, RSSI, MAC, weather, tests)
```

All UART traffic is non-blocking. Frames from the module are parsed incrementally in `loop()`. Requests that the MCU starts (time, RSSI, MAC, sync/record uploads, ...) go into a bounded queue that sends **one request at a time**, waits for the matching reply or a timeout (1.5 s; 7 s for sync/record uploads, because the module reports a failed upload only after about 5 s) and then sends the next. Repeated polls for the same data are merged into one frame. Requests are held until the module has finished its initialisation handshake, because the module ignores commands sent before that.

## Component options added in v2.0.0

```yaml
tuya_wifi_mcu:
  product_id: "xxxxxxxxxxxxxxxx"
  uart_id: tuya_uart
  wifi_control_mode: module
  wifi_led_pin: 16          # module pin numbers (module mode)
  wifi_reset_pin: 28
  wifi_ble_led_pin: 20      # optional 3rd work-mode byte (WiFi+BLE combo modules)

  pairing_mode: 0           # "m" in product info: 0 active (default) ... 5
  pairing_method: ez_ap     # optional "n": ez_ap or ap
  feature_config: '{"abv":8,"buf":1024}'   # optional 0x37 0x00 JSON, sent after the product query
  reset_notification: true  # receive local/app reset notifications (on_module_reset)
  batch_reports: true       # aggregate full-state reports into as few frames as possible

  weather:                  # optional; weather entities add their keys automatically
    keys: [w.sunrise, w.sunset]
    forecast_days: 1        # adds w.date.N
    time_format: local      # t.local or t.unix, added for sunrise/sunset
```

`batch_reports` matters for large boards. Without it, a state query from the module produces one frame per DP. With 64 relays at 9600 baud that is 64 × 12 bytes, sent as separate frames. With it, the same report is a single frame of about 320 bytes.

## DP entities

The `dp_id` and type of every entity must match the DP definition of the product on the Tuya IoT platform. Entities only report a DP once its value is known, from Tuya or locally. The component never invents a value.

### number: value DP that can be controlled

```yaml
number:
  - platform: tuya_wifi_mcu
    name: Max temperature
    dp_id: 135
    scale: 1               # Tuya "scale": 23.5 is sent as 235
    min_value: -40         # in real units
    max_value: 100
    step: 0.1
    bind_number_id: local_setpoint   # optional, two-way sync with another number
```

### select: enum DP

```yaml
select:
  - platform: tuya_wifi_mcu
    name: Power-on behaviour
    dp_id: 38
    options: ["off", "on", "memory"]   # list order = Tuya enum index (0, 1, 2)
    bind_select_id: local_policy        # optional
```

### text: string DP that can be written

```yaml
text:
  - platform: tuya_wifi_mcu
    name: Device label
    dp_id: 139
    max_length: 40
```

### sensor: numeric mirror of value / enum / bitmap / bool DPs

```yaml
sensor:
  - platform: tuya_wifi_mcu
    name: Temperature
    dp_id: 131
    scale: 1
    sensor_id: sht30_temperature   # upload this local sensor to Tuya
    report_delta: 0.3              # only upload changes of at least 0.3
  - platform: tuya_wifi_mcu
    name: Fault bits
    type: bitmap
    dp_id: 140
    bitmap_width: 1                # 1, 2 or 4 bytes as defined on Tuya
  - platform: tuya_wifi_mcu
    type: enum                     # or bool
    name: Alarm state
    dp_id: 137
```

### text_sensor: string or raw DP

```yaml
text_sensor:
  - platform: tuya_wifi_mcu
    name: Fault info
    dp_id: 146                     # type: string is the default
    text_sensor_id: local_faults   # optional upload source
  - platform: tuya_wifi_mcu
    name: Remote list
    type: raw                      # shown and accepted as hex text, e.g. "0a1bff"
    dp_id: 50
```

### Existing entities

`switch`, `binary_sensor` and `light` work exactly as documented above.

## Any DP from YAML: `on_dp_download` and upload actions

Some DPs have no matching ESPHome entity, for example raw IR/RF codes, schedules or composite commands. These can be handled directly:

```yaml
tuya_wifi_mcu:
  on_dp_download:
    - dp_id: 49                  # omit dp_id to receive every DP
      auto_acknowledge: false    # default true: echo the DP back as the acknowledgement
      then:
        - lambda: |-
            // dp_id (uint8_t), type (uint8_t), data (std::vector<uint8_t>)
            ESP_LOGI("remote", "remote_add %u bytes: %s", data.size(),
                     tuya_wifi_mcu::tuya_dp_as_string(type, data).c_str());
        - tuya_wifi_mcu.upload_raw:
            dp_id: 50
            value: !lambda return data;
```

When a DP is owned only by `on_dp_download` triggers, `auto_acknowledge` decides whether it is echoed back to the module. Set it to `false` when the automation uploads its own confirmation.

Upload actions are available for every DP type. Each one takes `dp_id`, `value` (a constant or a lambda), and optionally `mode` and `record_time`:

| Action | value |
| --- | --- |
| `tuya_wifi_mcu.upload_bool` | bool |
| `tuya_wifi_mcu.upload_value` | float, plus optional `scale` |
| `tuya_wifi_mcu.upload_enum` | 0-255 |
| `tuya_wifi_mcu.upload_bitmap` | uint32, plus `width` 1/2/4 |
| `tuya_wifi_mcu.upload_string` | string |
| `tuya_wifi_mcu.upload_raw` | hex string, byte list, or `std::vector<uint8_t>` lambda |

The `mode` option selects how the upload is delivered:

- **`async`** (default): sent as `0x07`.
- **`sync`**: sent as `0x22`. The result arrives in `on_upload_result`.
- **`record`**: sent as `0x34 0x0B`, a time-stamped record (energy, door-lock logs, ...).

`record_time` chooses the time stamp for a record: `module` uses the module's own clock, while `local` or `gmt` use an ESPHome `time_id`. If that clock is not valid yet, the module clock is used instead.

```yaml
- tuya_wifi_mcu.upload_value:
    dp_id: 17
    value: !lambda return id(energy_kwh).state;
    scale: 2
    mode: record
    record_time: local
    time_id: tuya_time
```

Helpers for lambdas, all in namespace `tuya_wifi_mcu`:

- `tuya_dp_as_int(type, data)`: integer value of a bool, enum, value or bitmap DP.
- `tuya_dp_as_string(type, data)`: text form; raw DPs come back as hex.
- `tuya_parse_hex(...)` and `tuya_format_hex(...)`: hex conversion.

## Module services

```yaml
time:
  - platform: tuya_wifi_mcu
    id: tuya_time
    source: gmt              # 0x0C (default). "local" uses 0x1C and needs
    timezone: Asia/Muscat    #   timezone: to match the paired location
    update_interval: 1h      # also synced as soon as the module is ready; retried every 10 s until valid

sensor:
  - platform: tuya_wifi_mcu
    type: wifi_rssi          # 0x24, dBm (optional command in Tuya firmware)
    name: Tuya RSSI
  - platform: tuya_wifi_mcu
    type: free_memory        # 0x0F
    name: Tuya module free memory
  - platform: tuya_wifi_mcu
    type: wifi_test          # 0x0E result, 0-100 %
    name: Tuya WiFi test
  - platform: tuya_wifi_mcu
    type: weather
    key: w.temp              # any numeric weather parameter
    day: 0                   # forecast day
    name: Outside temperature

text_sensor:
  - platform: tuya_wifi_mcu
    type: network_status     # 0x03 pushes + 0x2B polling
    name: Tuya network
  - platform: tuya_wifi_mcu
    type: mac_address        # 0x2D, requested once the module is ready
    name: Tuya MAC
  - platform: tuya_wifi_mcu
    type: weather
    key: w.sunrise
    name: Sunrise
```

### Actions

| Action | Effect |
| --- | --- |
| `tuya_wifi_mcu.reset_wifi` | Reset the module WiFi (`0x04`) |
| `tuya_wifi_mcu.start_pairing` | Start pairing (`0x05`); `mode: ez` or `ap` |
| `tuya_wifi_mcu.wifi_test` | Run the production WiFi test (`0x0E`) |
| `tuya_wifi_mcu.request_weather` | Request weather data now (`0x34 0x03`); the module allows one request per 20 min |
| `tuya_wifi_mcu.query_network_status` | Query the network status (`0x2B`) |
| `tuya_wifi_mcu.report_all` | Report the state of every DP entity |

### Triggers

All triggers live under `tuya_wifi_mcu:`.

| Trigger | Variables |
| --- | --- |
| `on_dp_download` | `dp_id`, `type`, `data` |
| `on_network_status` | `status` (0x00-0x06) |
| `on_weather` | `key`, `day`, `value` |
| `on_module_reset` | `reason` (0 local, 1 app, 2 app factory reset) |
| `on_upload_result` | `success`, `record` |
| `on_wifi_test` | `success`, `strength` |
| `on_module_ready` | none |

### Conditions

`tuya_wifi_mcu.is_cloud_connected` and `tuya_wifi_mcu.is_module_online` (a frame was received within the last 45 s).

Some services are optional in Tuya's module firmware, notably `0x24`, `0x0F` and record-type uploads. When a module does not answer, the request times out, the entity shows "unknown", and the queue moves on. Nothing blocks.

## Verification (v2.0.0)

| Suite | Result |
| --- | --- |
| `make host-test` | protocol, helper and services unit tests |
| `make e2e-test` | 28 original + 12 new host end-to-end tests (`tests/e2e/test_extended.py`), on ESPHome 2026.10.1 |
| `make config-test` | 22 configuration compatibility tests |
| `make compile-all` | adds the `esp-idf-extended` and `arduino-extended` fixtures, which use every v2 platform, trigger and action |

## Credits and license

MIT licensed (see `LICENSE`). Version 2.0.0 started from the v1.4.0 protocol implementation of `hzkincony/esphome-tuya-wifi-mcu` (MIT, © 2023 ChenHsingYu) and is maintained independently in this repository.
