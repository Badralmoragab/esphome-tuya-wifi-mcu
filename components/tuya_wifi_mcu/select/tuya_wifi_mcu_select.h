#pragma once

#include <string>
#include <vector>

#include "esphome/components/select/select.h"
#include "esphome/core/component.h"

#include "../tuya_wifi_mcu_entity.h"

namespace esphome {
namespace tuya_wifi_mcu {

/// ENUM DP. The position of each option in `options:` is its Tuya enum index,
/// which must follow the order of the enum range on the Tuya platform.
class TuyaWifiMcuSelect : public TuyaWifiMcuEntity, public Component, public select::Select {
 public:
  // Own copy of the options: the Select traits container type differs between
  // ESPHome releases, so index <-> text mapping never touches it.
  void add_tuya_option(const std::string &option) { this->tuya_options_.push_back(option); }
  void set_bind_select(select::Select *select) { this->bind_select_ = select; }

  TuyaDpType get_dp_type() const override { return TuyaDpType::ENUM; }

  void setup() override;
  void dump_config() override;
  bool process_dp_data(const uint8_t *value, uint16_t length) override;
  void report_tuya_dp_state() override;

 protected:
  void control(const std::string &value) override;
  int index_of_(const std::string &value) const;
  void apply_index_(size_t index, bool report);

  std::vector<std::string> tuya_options_;
  select::Select *bind_select_{nullptr};
  size_t index_{0};
  bool has_index_{false};
  bool syncing_{false};
};

}  // namespace tuya_wifi_mcu
}  // namespace esphome
