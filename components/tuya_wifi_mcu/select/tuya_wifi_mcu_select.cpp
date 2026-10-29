#include "tuya_wifi_mcu_select.h"

#include "../tuya_wifi_mcu_component.h"

namespace esphome {
namespace tuya_wifi_mcu {

void TuyaWifiMcuSelect::setup() {
  if (this->bind_select_ != nullptr) {
    // The (std::string, size_t) callback signature is not stable across
    // ESPHome versions, so the bound select's text is read back instead.
    this->bind_select_->add_on_state_callback([this](auto...) {
      if (this->syncing_) {
        return;
      }
      const auto active = this->bind_select_->active_index();
      if (!active.has_value()) {
        return;
      }
      const auto option = this->bind_select_->at(active.value());
      const int index = option.has_value() ? this->index_of_(option.value()) : -1;
      if (index >= 0) {
        this->apply_index_(static_cast<size_t>(index), true);
      }
    });
  }
}

int TuyaWifiMcuSelect::index_of_(const std::string &value) const {
  for (size_t i = 0; i < this->tuya_options_.size(); i++) {
    if (this->tuya_options_[i] == value) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void TuyaWifiMcuSelect::apply_index_(size_t index, bool report) {
  const bool changed = !this->has_index_ || index != this->index_;
  this->index_ = index;
  this->has_index_ = true;
  const std::string &option = this->tuya_options_[index];

  this->syncing_ = true;
  this->publish_state(option);
  const auto bound_index = this->bind_select_ != nullptr ? this->bind_select_->active_index() : optional<size_t>{};
  const auto bound_option =
      bound_index.has_value() ? this->bind_select_->at(bound_index.value()) : optional<std::string>{};
  if (this->bind_select_ != nullptr && (!bound_option.has_value() || bound_option.value() != option)) {
    auto call = this->bind_select_->make_call();
    call.set_option(option);
    call.perform();
  }
  this->syncing_ = false;

  if (report && changed && !this->is_processing_remote()) {
    this->report_tuya_dp_state();
  }
}

void TuyaWifiMcuSelect::control(const std::string &value) {
  const int index = this->index_of_(value);
  if (index >= 0) {
    this->apply_index_(static_cast<size_t>(index), true);
  }
}

bool TuyaWifiMcuSelect::process_dp_data(const uint8_t *value, uint16_t length) {
  if (length != 1 || value[0] >= this->tuya_options_.size()) {
    return false;
  }
  this->apply_index_(value[0], false);
  return true;
}

void TuyaWifiMcuSelect::report_tuya_dp_state() {
  if (this->parent_ != nullptr && this->has_index_) {
    this->parent_->report_enum_dp(this->get_dp_id(), static_cast<uint8_t>(this->index_));
  }
}

void TuyaWifiMcuSelect::dump_config() {
  ESP_LOGCONFIG(TAG, "Tuya WiFi MCU select (enum) DP %u, %u options", this->get_dp_id(),
                static_cast<unsigned>(this->tuya_options_.size()));
}

}  // namespace tuya_wifi_mcu
}  // namespace esphome
