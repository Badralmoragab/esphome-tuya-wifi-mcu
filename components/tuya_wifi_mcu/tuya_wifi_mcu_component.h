#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"

#include "tuya_protocol.h"
#include "tuya_services.h"
#include "tuya_wifi_mcu_entity.h"

namespace esphome {
namespace tuya_wifi_mcu {

static const char *const TAG = "tuya.wifi.mcu";

enum WifiControlMode : uint8_t {
  WIFI_CONTROL_MODE_MCU = 0,
  WIFI_CONTROL_MODE_MODULE = 1,
};

/// Pairing mode argument of WIFI_MODE (0x05).
enum TuyaPairingMode : uint8_t {
  TUYA_PAIRING_EZ = 0x00,
  TUYA_PAIRING_AP = 0x01,
};

class TuyaWifiMcuComponent;

/// Receives every downloaded DP unit, whether or not an entity owns the DP.
/// This is the generic escape hatch that lets one component serve any Tuya
/// product: DPs without a dedicated entity are handled in YAML automations.
class TuyaDpDownloadListener {
 public:
  /// Returns true when the listener consumed the DP (used to decide whether an
  /// otherwise-unknown DP is acknowledged and whether a warning is logged).
  virtual bool on_dp_download(uint8_t dp_id, TuyaDpType type, const std::vector<uint8_t> &value) = 0;
  /// When true, a DP consumed only by this listener is echoed back to the
  /// module as its acknowledgement (the Tuya SDK convention).
  virtual bool auto_acknowledge() const { return true; }
};

class TuyaWifiMcuComponent : public PollingComponent,
                            public uart::UARTDevice,
                            public TuyaProtocolListener {
 public:
  explicit TuyaWifiMcuComponent(uart::UARTComponent *uart_component)
      : PollingComponent(60000), UARTDevice(uart_component), protocol_(this) {}

  void setup() override;
  void dump_config() override;
  void update() override;
  void loop() override;
  float get_setup_priority() const override;

  void set_product_id(const std::string &product_id) { this->product_id_ = product_id.substr(0, 16); }
  void set_version(const std::string &mcu_version) { this->mcu_version_ = mcu_version.substr(0, 5); }
  void set_wifi_control_mode(WifiControlMode mode) { this->wifi_control_mode_ = mode; }
  void set_wifi_reset_pin(GPIOPin *wifi_reset_pin) { this->wifi_reset_pin_ = wifi_reset_pin; }
  void set_wifi_led_pin(GPIOPin *wifi_led_pin) { this->wifi_led_pin_ = wifi_led_pin; }
  void set_legacy_wifi_reset_pin(uint8_t pin) { this->legacy_wifi_reset_pin_ = pin; }
  void set_legacy_wifi_led_pin(uint8_t pin) { this->legacy_wifi_led_pin_ = pin; }
  void set_module_wifi_reset_pin(uint8_t wifi_reset_pin) { this->module_wifi_reset_pin_ = wifi_reset_pin; }
  void set_module_wifi_led_pin(uint8_t wifi_led_pin) { this->module_wifi_led_pin_ = wifi_led_pin; }

  // ---- Extended configuration (all optional; defaults keep v1.4.0 behaviour) ----
  /// Third byte of the module-mode work-mode reply (BLE status LED pin of combo modules).
  void set_module_ble_led_pin(uint8_t pin) {
    this->module_ble_led_pin_ = pin;
    this->has_module_ble_led_pin_ = true;
  }
  /// "m" field of the product information (0 = active pairing ... 5).
  void set_pairing_mode(uint8_t mode) { this->pairing_mode_ = mode; }
  /// "n" field of the product information (0 = EZ+AP, 1 = AP only). Omitted when unset.
  void set_pairing_method(uint8_t method) {
    this->pairing_method_ = method;
    this->has_pairing_method_ = true;
  }
  /// JSON sent with NEW_FEATURE_CONFIG (0x37 0x00) right after the product query.
  void set_feature_config(const std::string &json) { this->feature_config_ = json; }
  /// Enables module reset notifications (0x34 0x04 / 0x34 0x05).
  void set_reset_notification(bool enable) { this->reset_notification_ = enable; }
  /// Aggregates full-state reports into as few DP_UPLOAD frames as possible.
  void set_batch_reports(bool enable) { this->batch_reports_ = enable; }
  void add_weather_key(const std::string &key);
  void set_weather_forecast_days(uint8_t days) { this->weather_forecast_days_ = days; }
  void set_weather_time_key(const std::string &key) { this->weather_time_key_ = key; }

  void reset_tuya_wifi();
  void report_tuya_dp_states();
  void report_bool_dp(uint8_t dp_id, bool value);
  void report_value_dp(uint8_t dp_id, uint32_t value);

  // ---- Generic DP uploads (async, 0x07) for every Tuya DP type ----
  void report_dp(uint8_t dp_id, TuyaDpType type, const uint8_t *value, uint16_t length);
  void report_enum_dp(uint8_t dp_id, uint8_t value);
  void report_bitmap_dp(uint8_t dp_id, uint32_t value, uint8_t width);
  void report_string_dp(uint8_t dp_id, const std::string &value);
  void report_raw_dp(uint8_t dp_id, const std::vector<uint8_t> &value);
  /// Sends a pre-built DP data set (see TuyaDpBuilder) as one DP_UPLOAD frame.
  void upload_dp_data(const std::vector<uint8_t> &dp_data);
  /// Synchronous upload (0x22); the result arrives through on_upload_result.
  void upload_dp_data_sync(const std::vector<uint8_t> &dp_data);
  /// Record-type upload (0x34 0x0B); time may be nullptr for TuyaRecordTime::MODULE.
  void upload_dp_data_record(const std::vector<uint8_t> &dp_data, TuyaRecordTime mode,
                             const TuyaDateTime *time = nullptr);

  // ---- Module services (MCU-initiated requests, queued and non-blocking) ----
  using TimeCallback = std::function<void(bool success, const TuyaDateTime &time)>;
  void request_local_time(TimeCallback callback);
  void request_gmt_time(TimeCallback callback);
  void request_network_status();
  void request_wifi_rssi();
  void request_mac();
  void request_free_memory();
  void start_wifi_test();
  void open_weather();
  void request_weather();
  void start_pairing(TuyaPairingMode mode);

  // ---- State accessors (usable from YAML lambdas) ----
  uint8_t get_wifi_state() const { return this->wifi_work_state_; }
  bool is_cloud_connected() const { return this->wifi_work_state_ == 0x04; }
  bool is_module_ready() const { return this->module_ready_; }
  bool is_module_online() const;

  // ---- Listener registration ----
  void add_on_network_status_callback(std::function<void(uint8_t)> &&cb) {
    this->network_status_callbacks_.push_back(std::move(cb));
  }
  void add_on_rssi_callback(std::function<void(bool, int8_t)> &&cb) {
    this->rssi_callbacks_.push_back(std::move(cb));
  }
  void add_on_mac_callback(std::function<void(const std::string &)> &&cb) {
    this->mac_callbacks_.push_back(std::move(cb));
  }
  void add_on_free_memory_callback(std::function<void(uint32_t)> &&cb) {
    this->free_memory_callbacks_.push_back(std::move(cb));
  }
  void add_on_wifi_test_callback(std::function<void(bool, uint8_t)> &&cb) {
    this->wifi_test_callbacks_.push_back(std::move(cb));
  }
  void add_on_weather_callback(std::function<void(const TuyaWeatherItem &)> &&cb) {
    this->weather_callbacks_.push_back(std::move(cb));
  }
  void add_on_module_reset_callback(std::function<void(uint8_t)> &&cb) {
    this->module_reset_callbacks_.push_back(std::move(cb));
  }
  void add_on_upload_result_callback(std::function<void(bool, bool)> &&cb) {
    this->upload_result_callbacks_.push_back(std::move(cb));
  }
  void add_on_module_ready_callback(std::function<void()> &&cb) {
    this->module_ready_callbacks_.push_back(std::move(cb));
  }
  void add_dp_download_listener(TuyaDpDownloadListener *listener) {
    this->download_listeners_.push_back(listener);
  }

  void register_tuya_wifi_mcu_entity(TuyaWifiMcuEntity *entity) {
    entity->set_parent(this);
    this->entities_.push_back(entity);
  }

  void on_tuya_frame(const TuyaFrame &frame) override;

 protected:
  // Raised from 64 so that string/raw DPs and aggregated reports for large
  // installations (e.g. 64 relays + sensors) fit in one frame. The parser's
  // receive capacity is the same 1024 bytes, so both directions are symmetric.
  static constexpr uint16_t MAX_TX_PAYLOAD_SIZE = 1024;
  static constexpr size_t MAX_PENDING_REQUESTS = 16;
  static constexpr uint32_t REQUEST_TIMEOUT_MS = 1500;
  // The module answers a failed sync/record upload only after ~5 s.
  static constexpr uint32_t UPLOAD_RESULT_TIMEOUT_MS = 7000;
  static constexpr uint32_t REQUEST_GAP_MS = 50;
  static constexpr uint32_t MODULE_OFFLINE_TIMEOUT_MS = 45000;

  /// One MCU-initiated request waiting for its module response. Requests are
  /// sent strictly one at a time from loop(), so no call ever blocks; repeated
  /// polling requests for the same command are coalesced into one frame.
  struct PendingRequest {
    TuyaCommand command;
    std::vector<uint8_t> payload;
    TuyaCommand response;
    int16_t response_sub_command;  // -1: any payload; otherwise payload[0] must match
    uint32_t timeout_ms;
    bool coalesce;
    std::vector<std::function<void(bool, const uint8_t *, uint16_t)>> callbacks;
  };

  void send_frame_(TuyaCommand command, const uint8_t *payload = nullptr, uint16_t payload_length = 0);
  void send_frame_with_version_(uint8_t version, TuyaCommand command, const uint8_t *payload,
                                uint16_t payload_length);
  void process_dp_download_(const uint8_t *payload, uint16_t payload_length);
  bool validate_dp_payload_(const uint8_t *payload, uint16_t payload_length) const;
  void handle_reset_button_(uint32_t now);
  void update_wifi_led_(uint32_t now);

  void enqueue_request_(TuyaCommand command, std::vector<uint8_t> payload, TuyaCommand response,
                        int16_t response_sub_command, uint32_t timeout_ms, bool coalesce,
                        std::function<void(bool, const uint8_t *, uint16_t)> &&callback);
  void process_request_queue_(uint32_t now);
  bool match_pending_response_(const TuyaFrame &frame);
  void request_time_(TuyaCommand command, TimeCallback &&callback);
  void mark_module_ready_();
  void set_network_status_(uint8_t status);
  void handle_extended_service_(const TuyaFrame &frame);
  void handle_weather_data_(const TuyaFrame &frame);
  void flush_batch_();

  TuyaProtocolParser protocol_;
  std::string product_id_;
  std::string mcu_version_{"1.0.0"};
  WifiControlMode wifi_control_mode_{WIFI_CONTROL_MODE_MCU};
  GPIOPin *wifi_reset_pin_{nullptr};
  GPIOPin *wifi_led_pin_{nullptr};
  uint8_t legacy_wifi_reset_pin_{0};
  uint8_t legacy_wifi_led_pin_{0};
  uint8_t module_wifi_reset_pin_{0};
  uint8_t module_wifi_led_pin_{0};
  uint8_t wifi_work_state_{0xFF};
  bool first_heartbeat_{true};
  bool wifi_led_state_{false};
  uint32_t last_wifi_led_state_change_time_{0};
  bool reset_raw_pressed_{false};
  bool reset_stable_pressed_{false};
  bool reset_press_handled_{false};
  uint32_t reset_transition_time_{0};
  std::vector<TuyaWifiMcuEntity *> entities_;
  std::array<uint8_t, MAX_TX_PAYLOAD_SIZE + 7> tx_buffer_{};

  // Extended configuration
  uint8_t module_ble_led_pin_{0};
  bool has_module_ble_led_pin_{false};
  uint8_t pairing_mode_{0};
  uint8_t pairing_method_{0};
  bool has_pairing_method_{false};
  std::string feature_config_;
  bool reset_notification_{false};
  bool batch_reports_{false};
  std::vector<std::string> weather_keys_;
  uint8_t weather_forecast_days_{1};
  std::string weather_time_key_{"t.local"};

  // Runtime state
  bool module_ready_{false};
  bool weather_opened_{false};
  uint32_t last_rx_time_{0};
  bool rx_seen_{false};
  std::deque<PendingRequest> requests_;
  bool request_in_flight_{false};
  uint32_t request_sent_time_{0};
  uint32_t last_request_done_time_{0};
  bool batching_{false};
  TuyaDpBuilder batch_;

  // Listeners. Plain std::function vectors keep the component independent of
  // the CallbackManager API, which changed across ESPHome releases.
  std::vector<std::function<void(uint8_t)>> network_status_callbacks_;
  std::vector<std::function<void(bool, int8_t)>> rssi_callbacks_;
  std::vector<std::function<void(const std::string &)>> mac_callbacks_;
  std::vector<std::function<void(uint32_t)>> free_memory_callbacks_;
  std::vector<std::function<void(bool, uint8_t)>> wifi_test_callbacks_;
  std::vector<std::function<void(const TuyaWeatherItem &)>> weather_callbacks_;
  std::vector<std::function<void(uint8_t)>> module_reset_callbacks_;
  std::vector<std::function<void(bool, bool)>> upload_result_callbacks_;
  std::vector<std::function<void()>> module_ready_callbacks_;
  std::vector<TuyaDpDownloadListener *> download_listeners_;
};

}  // namespace tuya_wifi_mcu
}  // namespace esphome
