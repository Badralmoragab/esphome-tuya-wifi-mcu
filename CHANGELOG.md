# Changelog

## v2.0.0

Independent release based on the v1.4.0 protocol code of hzkincony/esphome-tuya-wifi-mcu (MIT), extended into a generic Tuya MCU protocol component. All changes are additive; v1.4.0 YAML and C++ names are unchanged.

### Added
- DP platforms: `number` (value), `select` (enum), `text` (string), `sensor` (value/enum/bitmap/bool), `text_sensor` (string/raw).
- `on_dp_download` trigger and `upload_bool/value/enum/bitmap/string/raw` actions with async, sync (`0x22`) and record-type (`0x34 0x0B`) delivery.
- Module services: time platform (`0x0C`/`0x1C`), network status (`0x2B`), RSSI (`0x24`), MAC (`0x2D`), free memory (`0x0F`), WiFi test (`0x0E`), weather (`0x20`/`0x21`/`0x34 0x03`), reset notifications (`0x34 0x04/0x05`), feature configuration (`0x37`).
- Options: `wifi_ble_led_pin`, `pairing_mode`, `pairing_method`, `feature_config`, `reset_notification`, `batch_reports`, `weather`.
- Triggers `on_network_status`, `on_weather`, `on_module_reset`, `on_upload_result`, `on_wifi_test`, `on_module_ready`; conditions `is_cloud_connected`, `is_module_online`.
- Non-blocking request queue (one request in flight, timeouts, coalescing, gated on module initialisation).
- Tests: `tests/protocol/test_tuya_services.cpp`, `tests/e2e/test_extended.py`, compile fixtures `esp-idf-extended` and `arduino-extended`.

### Changed
- Transmit capacity raised from 64 to 1024 bytes (matches the receive capacity).
- Pinned ESPHome for local/CI validation: 2026.10.1.

### Fixed
- Tuya switch now publishes its restore-mode state at boot. ESPHome 2026.x otherwise reports it as "unknown", which made two v1.4.0 e2e tests fail on current ESPHome.

### Not included
- MCU firmware OTA through the Tuya cloud (`0x0A`/`0x0B`); use ESPHome OTA.
- Serial pairing (`0x2A`), WiFi connection test (`0x2C`), module info (`0x34 0x07`). They can be added later on the same request queue.
