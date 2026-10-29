#pragma once

#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"

namespace esphome {
namespace tuya_wifi_mcu {

class TuyaWifiMcuComponent;

/// Real-time clock synchronised from the Tuya module (cloud time).
///  - source GMT   -> GET_GMT_TIME (0x0C): UTC, independent of time zones.
///  - source LOCAL -> GET_LOCAL_TIME (0x1C): the account's local time; it is
///    converted back to UTC with this clock's `timezone:`, which must match the
///    time zone of the location the device was paired in.
class TuyaWifiMcuTime : public time::RealTimeClock {
 public:
  enum Source : uint8_t { SOURCE_GMT = 0, SOURCE_LOCAL = 1 };

  void set_parent(TuyaWifiMcuComponent *parent) { this->parent_ = parent; }
  void set_source(Source source) { this->source_ = source; }

  void setup() override;
  void update() override;
  void dump_config() override;

 protected:
  void schedule_retry_();

  TuyaWifiMcuComponent *parent_{nullptr};
  Source source_{SOURCE_GMT};
  bool synced_once_{false};
};

}  // namespace tuya_wifi_mcu
}  // namespace esphome
