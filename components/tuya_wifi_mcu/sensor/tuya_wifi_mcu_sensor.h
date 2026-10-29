#pragma once

#include <string>

#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"

#include "../tuya_wifi_mcu_entity.h"

namespace esphome {
namespace tuya_wifi_mcu {

class TuyaWifiMcuComponent;

/// Numeric DP mirror: VALUE (with scale), ENUM, BITMAP or BOOLEAN shown as a
/// number. Downloads are published; with `sensor_id` a local sensor (SHT30,
/// ADS1115, energy meter...) is uploaded to Tuya, rate-limited by report_delta.
class TuyaWifiMcuSensor : public TuyaWifiMcuEntity, public Component, public sensor::Sensor {
 public:
  void set_dp_type(TuyaDpType type) { this->dp_type_ = type; }
  void set_scale(uint8_t scale) { this->scale_ = scale; }
  void set_bitmap_width(uint8_t width) { this->bitmap_width_ = width; }
  void set_source_sensor(sensor::Sensor *source) { this->source_ = source; }
  void set_report_delta(float delta) { this->report_delta_ = delta; }

  TuyaDpType get_dp_type() const override { return this->dp_type_; }

  void setup() override;
  void dump_config() override;
  bool process_dp_data(const uint8_t *value, uint16_t length) override;
  void report_tuya_dp_state() override;

 protected:
  void handle_local_value_(float value);
  float raw_to_state_(int32_t raw) const;

  TuyaDpType dp_type_{TuyaDpType::VALUE};
  uint8_t scale_{0};
  uint8_t bitmap_width_{4};
  sensor::Sensor *source_{nullptr};
  float report_delta_{0.0f};
  int32_t raw_value_{0};
  bool has_raw_value_{false};
  float last_reported_{0.0f};
  bool has_reported_{false};
};

/// Module service values that are not DPs.
enum class TuyaServiceSensorType : uint8_t {
  WIFI_RSSI,     ///< 0x24, dBm of the current router connection
  FREE_MEMORY,   ///< 0x0F, free heap of the WiFi module in bytes
  WIFI_TEST,     ///< 0x0E, production scan result 0-100 %
  WEATHER,       ///< 0x21, numeric weather parameter
};

class TuyaWifiMcuServiceSensor : public PollingComponent, public sensor::Sensor {
 public:
  void set_parent(TuyaWifiMcuComponent *parent) { this->parent_ = parent; }
  void set_service_type(TuyaServiceSensorType type) { this->service_type_ = type; }
  void set_weather_key(const std::string &key) { this->weather_key_ = key; }
  void set_weather_day(uint8_t day) { this->weather_day_ = day; }

  void setup() override;
  void update() override;
  void dump_config() override;

 protected:
  TuyaWifiMcuComponent *parent_{nullptr};
  TuyaServiceSensorType service_type_{TuyaServiceSensorType::WIFI_RSSI};
  std::string weather_key_;
  uint8_t weather_day_{0};
};

}  // namespace tuya_wifi_mcu
}  // namespace esphome
