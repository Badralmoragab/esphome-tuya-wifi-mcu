#pragma once

// YAML automation glue: triggers (on_dp_download, on_network_status, ...),
// actions (tuya_wifi_mcu.upload_*, wifi_test, start_pairing, ...) and
// conditions (tuya_wifi_mcu.is_cloud_connected / is_module_online).

#include <string>
#include <vector>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"

#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif

#include "tuya_services.h"
#include "tuya_wifi_mcu_component.h"

namespace esphome {
namespace tuya_wifi_mcu {

// ---------------------------------------------------------------------------
// Triggers
// ---------------------------------------------------------------------------

/// Fires for every downloaded DP unit (optionally filtered by dp_id). For DPs
/// without an entity this is how any product-specific command is handled.
class DpDownloadTrigger : public Trigger<uint8_t, uint8_t, std::vector<uint8_t>>, public TuyaDpDownloadListener {
 public:
  explicit DpDownloadTrigger(TuyaWifiMcuComponent *parent) { parent->add_dp_download_listener(this); }
  void set_dp_id(uint8_t dp_id) {
    this->dp_id_ = dp_id;
    this->has_dp_id_ = true;
  }
  void set_auto_acknowledge(bool ack) { this->auto_ack_ = ack; }

  bool on_dp_download(uint8_t dp_id, TuyaDpType type, const std::vector<uint8_t> &value) override {
    if (this->has_dp_id_ && dp_id != this->dp_id_) {
      return false;
    }
    this->trigger(dp_id, static_cast<uint8_t>(type), value);
    return true;
  }
  bool auto_acknowledge() const override { return this->auto_ack_; }

 protected:
  uint8_t dp_id_{0};
  bool has_dp_id_{false};
  bool auto_ack_{true};
};

class NetworkStatusTrigger : public Trigger<uint8_t> {
 public:
  explicit NetworkStatusTrigger(TuyaWifiMcuComponent *parent) {
    parent->add_on_network_status_callback([this](uint8_t status) { this->trigger(status); });
  }
};

class WeatherTrigger : public Trigger<std::string, uint8_t, std::string> {
 public:
  explicit WeatherTrigger(TuyaWifiMcuComponent *parent) {
    parent->add_on_weather_callback(
        [this](const TuyaWeatherItem &item) { this->trigger(item.key, item.day, item.string_value); });
  }
};

class ModuleResetTrigger : public Trigger<uint8_t> {
 public:
  explicit ModuleResetTrigger(TuyaWifiMcuComponent *parent) {
    parent->add_on_module_reset_callback([this](uint8_t reason) { this->trigger(reason); });
  }
};

class UploadResultTrigger : public Trigger<bool, bool> {
 public:
  explicit UploadResultTrigger(TuyaWifiMcuComponent *parent) {
    parent->add_on_upload_result_callback([this](bool success, bool record) { this->trigger(success, record); });
  }
};

class WifiTestTrigger : public Trigger<bool, uint8_t> {
 public:
  explicit WifiTestTrigger(TuyaWifiMcuComponent *parent) {
    parent->add_on_wifi_test_callback([this](bool success, uint8_t value) { this->trigger(success, value); });
  }
};

class ModuleReadyTrigger : public Trigger<> {
 public:
  explicit ModuleReadyTrigger(TuyaWifiMcuComponent *parent) {
    parent->add_on_module_ready_callback([this]() { this->trigger(); });
  }
};

// ---------------------------------------------------------------------------
// Upload actions (one per DP type, all sharing the delivery mode logic)
// ---------------------------------------------------------------------------

enum class TuyaUploadMode : uint8_t { ASYNC = 0, SYNC = 1, RECORD = 2 };

template<typename... Ts> class UploadActionBase : public Action<Ts...>, public Parented<TuyaWifiMcuComponent> {
 public:
  void set_dp_id(uint8_t dp_id) { this->dp_id_ = dp_id; }
  void set_mode(TuyaUploadMode mode) { this->mode_ = mode; }
  void set_record_time(TuyaRecordTime record_time) { this->record_time_ = record_time; }
#ifdef USE_TIME
  void set_time(time::RealTimeClock *clock) { this->time_ = clock; }
#endif

 protected:
  void deliver_(const TuyaDpBuilder &builder) {
    switch (this->mode_) {
      case TuyaUploadMode::SYNC:
        this->parent_->upload_dp_data_sync(builder.data());
        return;
      case TuyaUploadMode::RECORD: {
        TuyaRecordTime mode = this->record_time_;
        TuyaDateTime stamp;
        bool have_time = false;
#ifdef USE_TIME
        if (this->time_ != nullptr && mode != TuyaRecordTime::MODULE) {
          const ESPTime now = mode == TuyaRecordTime::LOCAL ? this->time_->now() : this->time_->utcnow();
          if (now.is_valid()) {
            stamp.year = now.year;
            stamp.month = now.month;
            stamp.day = now.day_of_month;
            stamp.hour = now.hour;
            stamp.minute = now.minute;
            stamp.second = now.second;
            have_time = true;
          }
        }
#endif
        if (!have_time) {
          // Without a valid MCU clock the module's own synced clock is used.
          mode = TuyaRecordTime::MODULE;
        }
        this->parent_->upload_dp_data_record(builder.data(), mode, have_time ? &stamp : nullptr);
        return;
      }
      default:
        this->parent_->upload_dp_data(builder.data());
        return;
    }
  }

  uint8_t dp_id_{0};
  TuyaUploadMode mode_{TuyaUploadMode::ASYNC};
  TuyaRecordTime record_time_{TuyaRecordTime::MODULE};
#ifdef USE_TIME
  time::RealTimeClock *time_{nullptr};
#endif
};

template<typename... Ts> class UploadBoolAction : public UploadActionBase<Ts...> {
 public:
  TEMPLATABLE_VALUE(bool, value)
  void play(const Ts &...x) override {
    TuyaDpBuilder builder;
    builder.add_bool(this->dp_id_, this->value_.value(x...));
    this->deliver_(builder);
  }
};

template<typename... Ts> class UploadValueAction : public UploadActionBase<Ts...> {
 public:
  TEMPLATABLE_VALUE(float, value)
  void set_scale(uint8_t scale) { this->scale_ = scale; }
  void play(const Ts &...x) override {
    float factor = 1.0f;
    for (uint8_t i = 0; i < this->scale_; i++)
      factor *= 10.0f;
    TuyaDpBuilder builder;
    builder.add_value(this->dp_id_, static_cast<int32_t>(std::lround(this->value_.value(x...) * factor)));
    this->deliver_(builder);
  }

 protected:
  uint8_t scale_{0};
};

template<typename... Ts> class UploadEnumAction : public UploadActionBase<Ts...> {
 public:
  TEMPLATABLE_VALUE(uint8_t, value)
  void play(const Ts &...x) override {
    TuyaDpBuilder builder;
    builder.add_enum(this->dp_id_, this->value_.value(x...));
    this->deliver_(builder);
  }
};

template<typename... Ts> class UploadBitmapAction : public UploadActionBase<Ts...> {
 public:
  TEMPLATABLE_VALUE(uint32_t, value)
  void set_width(uint8_t width) { this->width_ = width; }
  void play(const Ts &...x) override {
    TuyaDpBuilder builder;
    builder.add_bitmap(this->dp_id_, this->value_.value(x...), this->width_);
    this->deliver_(builder);
  }

 protected:
  uint8_t width_{4};
};

template<typename... Ts> class UploadStringAction : public UploadActionBase<Ts...> {
 public:
  TEMPLATABLE_VALUE(std::string, value)
  void play(const Ts &...x) override {
    TuyaDpBuilder builder;
    builder.add_string(this->dp_id_, this->value_.value(x...));
    this->deliver_(builder);
  }
};

template<typename... Ts> class UploadRawAction : public UploadActionBase<Ts...> {
 public:
  TEMPLATABLE_VALUE(std::vector<uint8_t>, value)
  void play(const Ts &...x) override {
    TuyaDpBuilder builder;
    builder.add_raw(this->dp_id_, this->value_.value(x...));
    this->deliver_(builder);
  }
};

// ---------------------------------------------------------------------------
// Module service actions
// ---------------------------------------------------------------------------

template<typename... Ts> class ResetWifiAction : public Action<Ts...>, public Parented<TuyaWifiMcuComponent> {
 public:
  void play(const Ts &...) override { this->parent_->reset_tuya_wifi(); }
};

template<typename... Ts> class StartPairingAction : public Action<Ts...>, public Parented<TuyaWifiMcuComponent> {
 public:
  void set_pairing_mode(TuyaPairingMode mode) { this->mode_ = mode; }
  void play(const Ts &...) override { this->parent_->start_pairing(this->mode_); }

 protected:
  TuyaPairingMode mode_{TUYA_PAIRING_EZ};
};

template<typename... Ts> class WifiTestAction : public Action<Ts...>, public Parented<TuyaWifiMcuComponent> {
 public:
  void play(const Ts &...) override { this->parent_->start_wifi_test(); }
};

template<typename... Ts> class RequestWeatherAction : public Action<Ts...>, public Parented<TuyaWifiMcuComponent> {
 public:
  void play(const Ts &...) override { this->parent_->request_weather(); }
};

template<typename... Ts> class ReportAllAction : public Action<Ts...>, public Parented<TuyaWifiMcuComponent> {
 public:
  void play(const Ts &...) override { this->parent_->report_tuya_dp_states(); }
};

template<typename... Ts>
class QueryNetworkStatusAction : public Action<Ts...>, public Parented<TuyaWifiMcuComponent> {
 public:
  void play(const Ts &...) override { this->parent_->request_network_status(); }
};

// ---------------------------------------------------------------------------
// Conditions
// ---------------------------------------------------------------------------

template<typename... Ts> class CloudConnectedCondition : public Condition<Ts...>, public Parented<TuyaWifiMcuComponent> {
 public:
  bool check(const Ts &...) override { return this->parent_->is_cloud_connected(); }
};

template<typename... Ts> class ModuleOnlineCondition : public Condition<Ts...>, public Parented<TuyaWifiMcuComponent> {
 public:
  bool check(const Ts &...) override { return this->parent_->is_module_online(); }
};

}  // namespace tuya_wifi_mcu
}  // namespace esphome
