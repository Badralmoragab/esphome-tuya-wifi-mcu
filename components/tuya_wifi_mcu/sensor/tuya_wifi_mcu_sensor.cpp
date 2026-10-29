#include "tuya_wifi_mcu_sensor.h"

#include <cmath>
#include <cstdlib>

#include "../tuya_scale.h"
#include "../tuya_wifi_mcu_component.h"

namespace esphome {
namespace tuya_wifi_mcu {

// ---------------------------------------------------------------------------
// DP sensor
// ---------------------------------------------------------------------------

void TuyaWifiMcuSensor::setup() {
  if (this->source_ != nullptr) {
    this->source_->add_on_state_callback([this](float value) { this->handle_local_value_(value); });
  }
}

float TuyaWifiMcuSensor::raw_to_state_(int32_t raw) const {
  if (this->dp_type_ == TuyaDpType::VALUE) {
    return tuya_from_raw(raw, this->scale_);
  }
  if (this->dp_type_ == TuyaDpType::BITMAP) {
    return static_cast<float>(static_cast<uint32_t>(raw));
  }
  return static_cast<float>(raw);
}

void TuyaWifiMcuSensor::handle_local_value_(float value) {
  if (std::isnan(value)) {
    return;
  }
  int32_t raw;
  if (this->dp_type_ == TuyaDpType::VALUE) {
    raw = tuya_to_raw(value, this->scale_);
  } else if (this->dp_type_ == TuyaDpType::BOOLEAN) {
    raw = value != 0.0f ? 1 : 0;
  } else {
    raw = static_cast<int32_t>(std::lround(value));
  }
  this->raw_value_ = raw;
  this->has_raw_value_ = true;
  const float state = this->raw_to_state_(raw);
  this->publish_state(state);

  // report_delta implements the "only report meaningful changes" rule Tuya
  // recommends for sensor DPs (e.g. 0.3 C for temperature).
  if (this->has_reported_ && std::fabs(state - this->last_reported_) < this->report_delta_) {
    return;
  }
  this->report_tuya_dp_state();
}

bool TuyaWifiMcuSensor::process_dp_data(const uint8_t *value, uint16_t length) {
  int32_t raw;
  switch (this->dp_type_) {
    case TuyaDpType::VALUE:
      if (length != 4)
        return false;
      raw = static_cast<int32_t>(decode_tuya_value(value));
      break;
    case TuyaDpType::ENUM:
    case TuyaDpType::BOOLEAN:
      if (length != 1)
        return false;
      raw = value[0];
      break;
    case TuyaDpType::BITMAP: {
      if (length != 1 && length != 2 && length != 4)
        return false;
      uint32_t bits = 0;
      for (uint16_t i = 0; i < length; i++)
        bits = (bits << 8) | value[i];
      raw = static_cast<int32_t>(bits);
      break;
    }
    default:
      return false;
  }
  this->raw_value_ = raw;
  this->has_raw_value_ = true;
  this->publish_state(this->raw_to_state_(raw));
  return true;
}

void TuyaWifiMcuSensor::report_tuya_dp_state() {
  if (this->parent_ == nullptr || !this->has_raw_value_) {
    return;
  }
  switch (this->dp_type_) {
    case TuyaDpType::VALUE:
      this->parent_->report_value_dp(this->get_dp_id(), static_cast<uint32_t>(this->raw_value_));
      break;
    case TuyaDpType::ENUM:
      this->parent_->report_enum_dp(this->get_dp_id(), static_cast<uint8_t>(this->raw_value_));
      break;
    case TuyaDpType::BOOLEAN:
      this->parent_->report_bool_dp(this->get_dp_id(), this->raw_value_ != 0);
      break;
    case TuyaDpType::BITMAP:
      this->parent_->report_bitmap_dp(this->get_dp_id(), static_cast<uint32_t>(this->raw_value_),
                                      this->bitmap_width_);
      break;
    default:
      return;
  }
  this->last_reported_ = this->raw_to_state_(this->raw_value_);
  this->has_reported_ = true;
}

void TuyaWifiMcuSensor::dump_config() {
  LOG_SENSOR("", "Tuya WiFi MCU sensor", this);
  ESP_LOGCONFIG(TAG, "  DP %u, type %u, scale %u", this->get_dp_id(), static_cast<uint8_t>(this->dp_type_),
                this->scale_);
}

// ---------------------------------------------------------------------------
// Service sensor
// ---------------------------------------------------------------------------

void TuyaWifiMcuServiceSensor::setup() {
  if (this->parent_ == nullptr) {
    this->mark_failed();
    return;
  }
  switch (this->service_type_) {
    case TuyaServiceSensorType::WIFI_RSSI:
      this->parent_->add_on_rssi_callback([this](bool ok, int8_t rssi) {
        this->publish_state(ok ? static_cast<float>(rssi) : NAN);
      });
      break;
    case TuyaServiceSensorType::FREE_MEMORY:
      this->parent_->add_on_free_memory_callback(
          [this](uint32_t bytes) { this->publish_state(static_cast<float>(bytes)); });
      break;
    case TuyaServiceSensorType::WIFI_TEST:
      this->parent_->add_on_wifi_test_callback([this](bool ok, uint8_t value) {
        this->publish_state(ok ? static_cast<float>(value) : 0.0f);
      });
      break;
    case TuyaServiceSensorType::WEATHER:
      this->parent_->add_on_weather_callback([this](const TuyaWeatherItem &item) {
        if (item.key != this->weather_key_ || item.day != this->weather_day_) {
          return;
        }
        // Some numeric parameters arrive as text (e.g. w.windSpeed "1.5").
        this->publish_state(item.is_string ? std::strtof(item.string_value.c_str(), nullptr)
                                           : static_cast<float>(item.int_value));
      });
      break;
  }
}

void TuyaWifiMcuServiceSensor::update() {
  if (this->parent_ == nullptr) {
    return;
  }
  switch (this->service_type_) {
    case TuyaServiceSensorType::WIFI_RSSI:
      this->parent_->request_wifi_rssi();
      break;
    case TuyaServiceSensorType::FREE_MEMORY:
      this->parent_->request_free_memory();
      break;
    default:
      // WiFi test runs on demand (action); weather is pushed by the module.
      break;
  }
}

void TuyaWifiMcuServiceSensor::dump_config() {
  LOG_SENSOR("", "Tuya WiFi MCU service sensor", this);
  if (this->service_type_ == TuyaServiceSensorType::WEATHER) {
    ESP_LOGCONFIG(TAG, "  Weather: %s (day %u)", this->weather_key_.c_str(), this->weather_day_);
  }
}

}  // namespace tuya_wifi_mcu
}  // namespace esphome
