#include "tuya_wifi_mcu_text_sensor.h"

#include <vector>

#include "../tuya_services.h"
#include "../tuya_wifi_mcu_component.h"

namespace esphome {
namespace tuya_wifi_mcu {

// ---------------------------------------------------------------------------
// DP text sensor
// ---------------------------------------------------------------------------

void TuyaWifiMcuTextSensor::setup() {
  if (this->source_ != nullptr) {
    this->source_->add_on_state_callback([this](const std::string &value) {
      std::string canonical = value;
      if (this->dp_type_ == TuyaDpType::RAW) {
        std::vector<uint8_t> bytes;
        if (!tuya_parse_hex(value, bytes)) {
          ESP_LOGW(TAG, "DP %u: '%s' is not valid hex for a RAW DP", this->get_dp_id(), value.c_str());
          return;
        }
        canonical = tuya_format_hex(bytes);
      }
      const bool changed = !this->has_value_ || canonical != this->value_;
      this->value_ = canonical;
      this->has_value_ = true;
      this->publish_state(canonical);
      if (changed) {
        this->report_tuya_dp_state();
      }
    });
  }
}

bool TuyaWifiMcuTextSensor::process_dp_data(const uint8_t *value, uint16_t length) {
  if (this->dp_type_ == TuyaDpType::RAW) {
    this->value_ = tuya_format_hex(value, length);
  } else {
    this->value_.assign(reinterpret_cast<const char *>(value), length);
  }
  this->has_value_ = true;
  this->publish_state(this->value_);
  return true;
}

void TuyaWifiMcuTextSensor::report_tuya_dp_state() {
  if (this->parent_ == nullptr || !this->has_value_) {
    return;
  }
  if (this->dp_type_ == TuyaDpType::RAW) {
    std::vector<uint8_t> bytes;
    if (tuya_parse_hex(this->value_, bytes)) {
      this->parent_->report_raw_dp(this->get_dp_id(), bytes);
    }
  } else {
    this->parent_->report_string_dp(this->get_dp_id(), this->value_);
  }
}

void TuyaWifiMcuTextSensor::dump_config() {
  LOG_TEXT_SENSOR("", "Tuya WiFi MCU text sensor", this);
  ESP_LOGCONFIG(TAG, "  DP %u (%s)", this->get_dp_id(), this->dp_type_ == TuyaDpType::RAW ? "raw" : "string");
}

// ---------------------------------------------------------------------------
// Service text sensor
// ---------------------------------------------------------------------------

void TuyaWifiMcuServiceTextSensor::setup() {
  if (this->parent_ == nullptr) {
    this->mark_failed();
    return;
  }
  switch (this->service_type_) {
    case TuyaServiceTextSensorType::NETWORK_STATUS:
      this->parent_->add_on_network_status_callback(
          [this](uint8_t status) { this->publish_state(tuya_network_status_to_string(status)); });
      break;
    case TuyaServiceTextSensorType::MAC_ADDRESS:
      this->parent_->add_on_mac_callback([this](const std::string &mac) { this->publish_state(mac); });
      // The MAC never changes: ask as soon as the module has initialised.
      this->parent_->add_on_module_ready_callback([this]() { this->parent_->request_mac(); });
      break;
    case TuyaServiceTextSensorType::WEATHER:
      this->parent_->add_on_weather_callback([this](const TuyaWeatherItem &item) {
        if (item.key == this->weather_key_ && item.day == this->weather_day_) {
          this->publish_state(item.string_value);
        }
      });
      break;
  }
}

void TuyaWifiMcuServiceTextSensor::update() {
  if (this->parent_ == nullptr) {
    return;
  }
  if (this->service_type_ == TuyaServiceTextSensorType::NETWORK_STATUS) {
    this->parent_->request_network_status();
  } else if (this->service_type_ == TuyaServiceTextSensorType::MAC_ADDRESS) {
    this->parent_->request_mac();
  }
}

void TuyaWifiMcuServiceTextSensor::dump_config() {
  LOG_TEXT_SENSOR("", "Tuya WiFi MCU service text sensor", this);
  if (this->service_type_ == TuyaServiceTextSensorType::WEATHER) {
    ESP_LOGCONFIG(TAG, "  Weather: %s (day %u)", this->weather_key_.c_str(), this->weather_day_);
  }
}

}  // namespace tuya_wifi_mcu
}  // namespace esphome
