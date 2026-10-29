#pragma once

#include "esphome/components/number/number.h"
#include "esphome/core/component.h"

#include "../tuya_wifi_mcu_entity.h"

namespace esphome {
namespace tuya_wifi_mcu {

/// Controllable VALUE DP (set points, timers, percentages...). Scale converts
/// between the integer on the wire and the real value shown in ESPHome.
class TuyaWifiMcuNumber : public TuyaWifiMcuEntity, public Component, public number::Number {
 public:
  void set_scale(uint8_t scale) { this->scale_ = scale; }
  void set_bind_number(number::Number *number) { this->bind_number_ = number; }

  TuyaDpType get_dp_type() const override { return TuyaDpType::VALUE; }

  void setup() override;
  void dump_config() override;
  bool process_dp_data(const uint8_t *value, uint16_t length) override;
  void report_tuya_dp_state() override;

 protected:
  void control(float value) override;
  void apply_value_(float value, bool report);

  number::Number *bind_number_{nullptr};
  uint8_t scale_{0};
  int32_t raw_value_{0};
  bool has_raw_value_{false};
  bool syncing_{false};
};

}  // namespace tuya_wifi_mcu
}  // namespace esphome
