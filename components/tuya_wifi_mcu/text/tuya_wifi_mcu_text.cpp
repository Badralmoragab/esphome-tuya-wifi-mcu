#include "tuya_wifi_mcu_text.h"

#include "../tuya_wifi_mcu_component.h"

namespace esphome {
namespace tuya_wifi_mcu {

void TuyaWifiMcuText::control(const std::string &value) {
  const bool changed = !this->has_value_ || value != this->value_;
  this->value_ = value;
  this->has_value_ = true;
  this->publish_state(value);
  if (changed) {
    this->report_tuya_dp_state();
  }
}

bool TuyaWifiMcuText::process_dp_data(const uint8_t *value, uint16_t length) {
  this->value_.assign(reinterpret_cast<const char *>(value), length);
  this->has_value_ = true;
  this->publish_state(this->value_);
  return true;
}

void TuyaWifiMcuText::report_tuya_dp_state() {
  if (this->parent_ != nullptr && this->has_value_) {
    this->parent_->report_string_dp(this->get_dp_id(), this->value_);
  }
}

void TuyaWifiMcuText::dump_config() { ESP_LOGCONFIG(TAG, "Tuya WiFi MCU text (string) DP %u", this->get_dp_id()); }

}  // namespace tuya_wifi_mcu
}  // namespace esphome
