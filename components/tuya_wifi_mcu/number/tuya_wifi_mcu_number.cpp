#include "tuya_wifi_mcu_number.h"

#include <cmath>

#include "../tuya_scale.h"
#include "../tuya_wifi_mcu_component.h"

namespace esphome {
namespace tuya_wifi_mcu {

void TuyaWifiMcuNumber::setup() {
  if (this->bind_number_ != nullptr) {
    this->bind_number_->add_on_state_callback([this](float value) {
      if (this->syncing_ || std::isnan(value)) {
        return;
      }
      this->apply_value_(value, true);
    });
  }
}

void TuyaWifiMcuNumber::apply_value_(float value, bool report) {
  const int32_t raw = tuya_to_raw(value, this->scale_);
  const bool changed = !this->has_raw_value_ || raw != this->raw_value_;
  this->raw_value_ = raw;
  this->has_raw_value_ = true;
  const float scaled = tuya_from_raw(raw, this->scale_);

  this->syncing_ = true;
  this->publish_state(scaled);
  if (this->bind_number_ != nullptr && this->bind_number_->state != scaled) {
    auto call = this->bind_number_->make_call();
    call.set_value(scaled);
    call.perform();
  }
  this->syncing_ = false;

  if (report && changed && !this->is_processing_remote()) {
    this->report_tuya_dp_state();
  }
}

void TuyaWifiMcuNumber::control(float value) { this->apply_value_(value, true); }

bool TuyaWifiMcuNumber::process_dp_data(const uint8_t *value, uint16_t length) {
  if (length != 4) {
    return false;
  }
  const int32_t raw = static_cast<int32_t>(decode_tuya_value(value));
  this->apply_value_(tuya_from_raw(raw, this->scale_), false);
  return true;
}

void TuyaWifiMcuNumber::report_tuya_dp_state() {
  // Never invent a value: until the DP is known (from Tuya or locally) it is not reported.
  if (this->parent_ != nullptr && this->has_raw_value_) {
    this->parent_->report_value_dp(this->get_dp_id(), static_cast<uint32_t>(this->raw_value_));
  }
}

void TuyaWifiMcuNumber::dump_config() {
  ESP_LOGCONFIG(TAG, "Tuya WiFi MCU number DP %u (scale %u)", this->get_dp_id(), this->scale_);
}

}  // namespace tuya_wifi_mcu
}  // namespace esphome
