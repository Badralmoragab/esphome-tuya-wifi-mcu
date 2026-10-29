#pragma once

#include <string>

#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

#include "../tuya_wifi_mcu_entity.h"

namespace esphome {
namespace tuya_wifi_mcu {

class TuyaWifiMcuComponent;

/// STRING DP, or RAW DP shown/accepted as hex text. With `text_sensor_id` a
/// local text sensor is uploaded to Tuya whenever it changes.
class TuyaWifiMcuTextSensor : public TuyaWifiMcuEntity, public Component, public text_sensor::TextSensor {
 public:
  void set_dp_type(TuyaDpType type) { this->dp_type_ = type; }
  void set_source_text_sensor(text_sensor::TextSensor *source) { this->source_ = source; }

  TuyaDpType get_dp_type() const override { return this->dp_type_; }

  void setup() override;
  void dump_config() override;
  bool process_dp_data(const uint8_t *value, uint16_t length) override;
  void report_tuya_dp_state() override;

 protected:
  TuyaDpType dp_type_{TuyaDpType::STRING};
  text_sensor::TextSensor *source_{nullptr};
  std::string value_;  // STRING: text; RAW: canonical hex
  bool has_value_{false};
};

enum class TuyaServiceTextSensorType : uint8_t {
  NETWORK_STATUS,  ///< 0x03 pushes + 0x2B polling
  MAC_ADDRESS,     ///< 0x2D
  WEATHER,         ///< 0x21 parameter as text (sunrise, conditionNum, windDir...)
};

class TuyaWifiMcuServiceTextSensor : public PollingComponent, public text_sensor::TextSensor {
 public:
  void set_parent(TuyaWifiMcuComponent *parent) { this->parent_ = parent; }
  void set_service_type(TuyaServiceTextSensorType type) { this->service_type_ = type; }
  void set_weather_key(const std::string &key) { this->weather_key_ = key; }
  void set_weather_day(uint8_t day) { this->weather_day_ = day; }

  void setup() override;
  void update() override;
  void dump_config() override;

 protected:
  TuyaWifiMcuComponent *parent_{nullptr};
  TuyaServiceTextSensorType service_type_{TuyaServiceTextSensorType::NETWORK_STATUS};
  std::string weather_key_;
  uint8_t weather_day_{0};
};

}  // namespace tuya_wifi_mcu
}  // namespace esphome
