#include "tuya_wifi_mcu_time.h"

#include "esphome/core/log.h"

#include "../tuya_wifi_mcu_component.h"

namespace esphome {
namespace tuya_wifi_mcu {

// Until the first successful sync the module is asked again after this delay
// (the module answers "not synced" until it has reached the Tuya cloud).
static constexpr uint32_t TIME_RETRY_MS = 10000;

void TuyaWifiMcuTime::setup() {
  time::RealTimeClock::setup();
  if (this->parent_ == nullptr) {
    this->mark_failed();
    return;
  }
  // Ask as soon as the module is up instead of waiting a full update_interval.
  this->parent_->add_on_module_ready_callback([this]() { this->update(); });
}

void TuyaWifiMcuTime::update() {
  if (this->parent_ == nullptr) {
    return;
  }
  auto on_time = [this](bool success, const TuyaDateTime &t) {
    if (!success) {
      ESP_LOGD(TAG, "Tuya module has no valid time yet");
      this->schedule_retry_();
      return;
    }
    ESPTime esp_time{};
    esp_time.year = t.year;
    esp_time.month = t.month;
    esp_time.day_of_month = t.day;
    esp_time.hour = t.hour;
    esp_time.minute = t.minute;
    esp_time.second = t.second;
    if (this->source_ == SOURCE_LOCAL) {
      esp_time.recalc_timestamp_local();
    } else {
      esp_time.recalc_timestamp_utc(false);
    }
    if (esp_time.timestamp <= 0) {
      ESP_LOGW(TAG, "Tuya module returned an invalid time");
      this->schedule_retry_();
      return;
    }
    ESP_LOGD(TAG, "Tuya %s time %04u-%02u-%02u %02u:%02u:%02u -> epoch %ld",
             this->source_ == SOURCE_LOCAL ? "local" : "GMT", t.year, t.month, t.day, t.hour, t.minute, t.second,
             static_cast<long>(esp_time.timestamp));
    this->synced_once_ = true;
    this->synchronize_epoch_(static_cast<uint32_t>(esp_time.timestamp));
  };
  if (this->source_ == SOURCE_LOCAL) {
    this->parent_->request_local_time(on_time);
  } else {
    this->parent_->request_gmt_time(on_time);
  }
}

void TuyaWifiMcuTime::schedule_retry_() {
  if (!this->synced_once_) {
    this->set_timeout("tuya_time_retry", TIME_RETRY_MS, [this]() { this->update(); });
  }
}

void TuyaWifiMcuTime::dump_config() {
  ESP_LOGCONFIG(TAG, "Tuya WiFi MCU time (source: %s)", this->source_ == SOURCE_LOCAL ? "local" : "gmt");
  time::RealTimeClock::dump_config();
}

}  // namespace tuya_wifi_mcu
}  // namespace esphome
