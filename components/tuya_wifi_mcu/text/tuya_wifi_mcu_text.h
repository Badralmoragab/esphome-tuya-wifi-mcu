#pragma once

#include <string>

#include "esphome/components/text/text.h"
#include "esphome/core/component.h"

#include "../tuya_wifi_mcu_entity.h"

namespace esphome {
namespace tuya_wifi_mcu {

/// Writable STRING DP (names, IP addresses, schedules as text...).
class TuyaWifiMcuText : public TuyaWifiMcuEntity, public Component, public text::Text {
 public:
  TuyaDpType get_dp_type() const override { return TuyaDpType::STRING; }

  void dump_config() override;
  bool process_dp_data(const uint8_t *value, uint16_t length) override;
  void report_tuya_dp_state() override;

 protected:
  void control(const std::string &value) override;

  std::string value_;
  bool has_value_{false};
};

}  // namespace tuya_wifi_mcu
}  // namespace esphome
