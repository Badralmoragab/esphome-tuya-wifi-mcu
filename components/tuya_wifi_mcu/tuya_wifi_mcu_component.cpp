#include "tuya_wifi_mcu_component.h"

#include <algorithm>
#include <cstdio>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32
#include "esphome/components/esp32/gpio.h"
#endif

#include "tuya_dp_dispatch.h"

namespace esphome {
namespace tuya_wifi_mcu {

static constexpr uint8_t WIFI_CONN_CLOUD = 0x04;
static constexpr uint8_t WIFI_LOW_POWER = 0x05;
static constexpr uint8_t SMART_CONFIG = 0x00;
static constexpr uint32_t RESET_DEBOUNCE_MS = 20;
static constexpr uint32_t WIFI_LED_BLINK_INTERVAL_MS = 500;
static constexpr size_t UART_READ_BATCH_SIZE = 64;

static GPIOPin *make_legacy_wifi_pin(uint8_t number, bool output) {
#ifdef USE_ESP32
  if (GPIO_IS_VALID_GPIO(number) && (!output || GPIO_IS_VALID_OUTPUT_GPIO(number))) {
    auto *pin = new esp32::ESP32InternalGPIOPin();
    pin->set_pin(static_cast<gpio_num_t>(number));
    pin->set_flags(output ? gpio::FLAG_OUTPUT : gpio::FLAG_INPUT);
    pin->set_inverted(!output);
    pin->set_drive_strength(GPIO_DRIVE_CAP_2);
    return pin;
  }
#endif
  ESP_LOGW(TAG, "Ignoring unavailable WiFi %s GPIO %u", output ? "LED" : "reset", number);
  return nullptr;
}

void TuyaWifiMcuComponent::setup() {
  ESP_LOGD(TAG, "Setting up Tuya WiFi MCU component");

  for (auto *entity : this->entities_) {
    if (entity == nullptr) {
      ESP_LOGE(TAG, "Invalid Tuya entity registration");
      this->mark_failed();
      return;
    }
  }

  if (this->wifi_control_mode_ == WIFI_CONTROL_MODE_MCU) {
    if (this->legacy_wifi_reset_pin_ != 0) {
      this->wifi_reset_pin_ = make_legacy_wifi_pin(this->legacy_wifi_reset_pin_, false);
    }
    if (this->legacy_wifi_led_pin_ != 0) {
      this->wifi_led_pin_ = make_legacy_wifi_pin(this->legacy_wifi_led_pin_, true);
    }
    if (this->wifi_led_pin_ != nullptr) {
      this->wifi_led_pin_->setup();
      this->wifi_led_pin_->digital_write(false);
    }
    if (this->wifi_reset_pin_ != nullptr) {
      this->wifi_reset_pin_->setup();
    }
  }
}

void TuyaWifiMcuComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Tuya WiFi MCU:");
  ESP_LOGCONFIG(TAG, "  Product ID: %s", this->product_id_.c_str());
  ESP_LOGCONFIG(TAG, "  MCU version: %s", this->mcu_version_.c_str());
  ESP_LOGCONFIG(TAG, "  WiFi control mode: %s",
                this->wifi_control_mode_ == WIFI_CONTROL_MODE_MCU ? "MCU" : "module");
  ESP_LOGCONFIG(TAG, "  Registered DPs: %u", static_cast<unsigned>(this->entities_.size()));
  if (this->wifi_control_mode_ == WIFI_CONTROL_MODE_MCU) {
    LOG_PIN("  WiFi reset pin: ", this->wifi_reset_pin_);
    LOG_PIN("  WiFi LED pin: ", this->wifi_led_pin_);
  } else {
    ESP_LOGCONFIG(TAG, "  Module WiFi reset pin: %u", this->module_wifi_reset_pin_);
    ESP_LOGCONFIG(TAG, "  Module WiFi LED pin: %u", this->module_wifi_led_pin_);
    if (this->has_module_ble_led_pin_) {
      ESP_LOGCONFIG(TAG, "  Module BLE LED pin: %u", this->module_ble_led_pin_);
    }
  }
  ESP_LOGCONFIG(TAG, "  Pairing mode (m): %u", this->pairing_mode_);
  if (this->has_pairing_method_) {
    ESP_LOGCONFIG(TAG, "  Pairing method (n): %u", this->pairing_method_);
  }
  ESP_LOGCONFIG(TAG, "  Batch reports: %s", this->batch_reports_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Reset notification: %s", this->reset_notification_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  DP download listeners: %u", static_cast<unsigned>(this->download_listeners_.size()));
  for (const auto &key : this->weather_keys_) {
    ESP_LOGCONFIG(TAG, "  Weather parameter: %s", key.c_str());
  }
}

void TuyaWifiMcuComponent::loop() {
  std::array<uint8_t, UART_READ_BATCH_SIZE> buffer{};
  bool uart_idle = false;
  while (true) {
    const size_t available = this->available();
    if (available == 0) {
      uart_idle = true;
      break;
    }

    const size_t length = std::min(available, buffer.size());
    if (!this->read_array(buffer.data(), length)) {
      break;
    }

    const uint32_t read_time = millis();
    for (size_t i = 0; i < length; i++) {
      this->protocol_.feed(buffer[i], read_time);
    }
  }

  const uint32_t now = millis();
  if (uart_idle) {
    this->protocol_.check_timeout(now);
  }

  // MCU-initiated requests are dispatched here, one at a time, so neither
  // entity code nor automations ever wait on the UART.
  this->process_request_queue_(now);

  if (this->wifi_control_mode_ == WIFI_CONTROL_MODE_MCU) {
    this->handle_reset_button_(now);
    this->update_wifi_led_(now);
  }
}

void TuyaWifiMcuComponent::update() { this->report_tuya_dp_states(); }

void TuyaWifiMcuComponent::send_frame_(TuyaCommand command, const uint8_t *payload, uint16_t payload_length) {
  this->send_frame_with_version_(TUYA_MCU_FRAME_VERSION, command, payload, payload_length);
}

void TuyaWifiMcuComponent::send_frame_with_version_(uint8_t version, TuyaCommand command, const uint8_t *payload,
                                                    uint16_t payload_length) {
  const size_t frame_length = encode_tuya_frame_with_version(version, command, payload, payload_length,
                                                             this->tx_buffer_.data(), this->tx_buffer_.size());
  if (frame_length == 0) {
    ESP_LOGW(TAG, "Could not encode Tuya command 0x%02X", static_cast<uint8_t>(command));
    return;
  }
  this->write_array(this->tx_buffer_.data(), frame_length);
}

void TuyaWifiMcuComponent::on_tuya_frame(const TuyaFrame &frame) {
  ESP_LOGV(TAG, "Received Tuya command 0x%02X with %u payload bytes", static_cast<uint8_t>(frame.command),
           frame.payload_length);
  this->last_rx_time_ = millis();
  this->rx_seen_ = true;

  // Responses to MCU-initiated requests are matched first; the switch below
  // handles module-initiated frames.
  if (this->match_pending_response_(frame)) {
    return;
  }

  switch (frame.command) {
    case TuyaCommand::HEARTBEAT: {
      if (frame.payload_length != 0) {
        return;
      }
      const uint8_t heartbeat = this->first_heartbeat_ ? 0x00 : 0x01;
      this->first_heartbeat_ = false;
      this->send_frame_(TuyaCommand::HEARTBEAT, &heartbeat, 1);
      break;
    }

    case TuyaCommand::PRODUCT_QUERY: {
      if (frame.payload_length != 0) {
        return;
      }
      std::array<char, 96> product_info{};
      int length;
      if (this->has_pairing_method_) {
        length = snprintf(product_info.data(), product_info.size(), "{\"p\":\"%s\",\"v\":\"%s\",\"m\":%u,\"n\":%u}",
                          this->product_id_.c_str(), this->mcu_version_.c_str(), this->pairing_mode_,
                          this->pairing_method_);
      } else {
        length = snprintf(product_info.data(), product_info.size(), "{\"p\":\"%s\",\"v\":\"%s\",\"m\":%u}",
                          this->product_id_.c_str(), this->mcu_version_.c_str(), this->pairing_mode_);
      }
      if (length <= 0 || static_cast<size_t>(length) >= product_info.size()) {
        ESP_LOGE(TAG, "Tuya product information is too long");
        return;
      }
      this->send_frame_(TuyaCommand::PRODUCT_QUERY, reinterpret_cast<const uint8_t *>(product_info.data()),
                        static_cast<uint16_t>(length));
      // Spec: the optional feature configuration goes after 0x01 and before 0x02.
      if (!this->feature_config_.empty()) {
        std::vector<uint8_t> features = {0x00};
        features.insert(features.end(), this->feature_config_.begin(), this->feature_config_.end());
        this->send_frame_(TuyaCommand::NEW_FEATURE_CONFIG, features.data(), static_cast<uint16_t>(features.size()));
      }
      break;
    }

    case TuyaCommand::WORK_MODE_QUERY: {
      if (frame.payload_length != 0) {
        return;
      }
      if (this->wifi_control_mode_ == WIFI_CONTROL_MODE_MODULE) {
        const uint8_t pins[] = {this->module_wifi_led_pin_, this->module_wifi_reset_pin_, this->module_ble_led_pin_};
        this->send_frame_(TuyaCommand::WORK_MODE_QUERY, pins, this->has_module_ble_led_pin_ ? 3 : 2);
      } else {
        this->send_frame_(TuyaCommand::WORK_MODE_QUERY);
      }
      this->mark_module_ready_();
      break;
    }

    case TuyaCommand::WIFI_STATE:
      if (frame.payload_length != 1 || frame.payload[0] > 0x06) {
        return;
      }
      this->send_frame_(TuyaCommand::WIFI_STATE);
      this->mark_module_ready_();
      this->set_network_status_(frame.payload[0]);
      break;

    case TuyaCommand::WIFI_RESET:
      ESP_LOGD(TAG, "Tuya WiFi reset acknowledged");
      break;

    case TuyaCommand::WIFI_MODE:
      ESP_LOGD(TAG, "Tuya WiFi mode change acknowledged");
      break;

    case TuyaCommand::DP_DOWNLOAD:
      this->process_dp_download_(frame.payload, frame.payload_length);
      break;

    case TuyaCommand::DP_UPLOAD:
      break;

    case TuyaCommand::STATE_QUERY:
      if (frame.payload_length == 0) {
        this->report_tuya_dp_states();
      }
      break;

    case TuyaCommand::WEATHER_DATA:
      this->handle_weather_data_(frame);
      break;

    case TuyaCommand::EXTENDED_SERVICE:
      this->handle_extended_service_(frame);
      break;

    case TuyaCommand::NEW_FEATURE_CONFIG:
      if (frame.payload_length >= 2 && frame.payload[0] == 0x00) {
        if (frame.payload[1] == 0x00) {
          ESP_LOGD(TAG, "Tuya feature configuration accepted");
        } else {
          ESP_LOGW(TAG, "Tuya feature configuration rejected (code %u)", frame.payload[1]);
        }
      }
      break;

    default:
      ESP_LOGD(TAG, "Ignoring unsupported Tuya command 0x%02X", static_cast<uint8_t>(frame.command));
      break;
  }
}

bool TuyaWifiMcuComponent::validate_dp_payload_(const uint8_t *payload, uint16_t payload_length) const {
  uint16_t offset = 0;
  while (offset < payload_length) {
    if (payload_length - offset < 4) {
      return false;
    }
    const auto type = static_cast<TuyaDpType>(payload[offset + 1]);
    const uint16_t length = (static_cast<uint16_t>(payload[offset + 2]) << 8) | payload[offset + 3];
    offset += 4;
    if (length > payload_length - offset) {
      return false;
    }

    switch (type) {
      case TuyaDpType::RAW:
      case TuyaDpType::STRING:
        break;
      case TuyaDpType::BOOLEAN:
        if (length != 1 || payload[offset] > 1) {
          return false;
        }
        break;
      case TuyaDpType::VALUE:
        if (length != 4) {
          return false;
        }
        break;
      case TuyaDpType::ENUM:
        if (length != 1) {
          return false;
        }
        break;
      case TuyaDpType::BITMAP:
        if (length != 1 && length != 2 && length != 4) {
          return false;
        }
        break;
      default:
        return false;
    }
    offset += length;
  }
  return offset == payload_length && payload_length != 0;
}

void TuyaWifiMcuComponent::process_dp_download_(const uint8_t *payload, uint16_t payload_length) {
  if (!this->validate_dp_payload_(payload, payload_length)) {
    ESP_LOGW(TAG, "Ignoring malformed Tuya DP download");
    return;
  }

  uint16_t offset = 0;
  while (offset < payload_length) {
    const uint8_t dp_id = payload[offset];
    const auto type = static_cast<TuyaDpType>(payload[offset + 1]);
    const uint16_t length = (static_cast<uint16_t>(payload[offset + 2]) << 8) | payload[offset + 3];
    const uint8_t *value = payload + offset + 4;
    offset += 4 + length;

    const auto result = dispatch_tuya_dp(this->entities_, dp_id, type, value, length);

    // Generic listeners (on_dp_download automations) see every unit. When no
    // entity owns the DP they become its owner, including the acknowledgement.
    bool listener_handled = false;
    bool listener_ack = false;
    if (!this->download_listeners_.empty()) {
      const std::vector<uint8_t> data(value, value + length);
      for (auto *listener : this->download_listeners_) {
        if (listener->on_dp_download(dp_id, type, data)) {
          listener_handled = true;
          listener_ack = listener_ack || listener->auto_acknowledge();
        }
      }
    }
    if (result.id_matches == 0 && listener_handled) {
      if (listener_ack) {
        this->report_dp(dp_id, type, value, length);
      }
      continue;
    }

    if (result.id_matches == 0) {
      ESP_LOGW(TAG, "Ignoring unknown Tuya DP %u", dp_id);
    } else if (result.type_matches == 0) {
      ESP_LOGW(TAG, "Ignoring Tuya DP %u with unexpected type %u", dp_id, static_cast<uint8_t>(type));
    } else if (result.accepted == 0) {
      ESP_LOGW(TAG, "Tuya DP %u was rejected by all matching entities", dp_id);
    }
  }
}

void TuyaWifiMcuComponent::report_bool_dp(uint8_t dp_id, bool value) {
  const uint8_t byte = static_cast<uint8_t>(value);
  this->report_dp(dp_id, TuyaDpType::BOOLEAN, &byte, 1);
}

void TuyaWifiMcuComponent::report_value_dp(uint8_t dp_id, uint32_t value) {
  uint8_t bytes[4];
  encode_tuya_value(value, bytes);
  this->report_dp(dp_id, TuyaDpType::VALUE, bytes, 4);
}

void TuyaWifiMcuComponent::report_enum_dp(uint8_t dp_id, uint8_t value) {
  this->report_dp(dp_id, TuyaDpType::ENUM, &value, 1);
}

void TuyaWifiMcuComponent::report_bitmap_dp(uint8_t dp_id, uint32_t value, uint8_t width) {
  TuyaDpBuilder builder;
  builder.add_bitmap(dp_id, value, width);
  const auto &data = builder.data();
  this->report_dp(dp_id, TuyaDpType::BITMAP, data.data() + 4, static_cast<uint16_t>(data.size() - 4));
}

void TuyaWifiMcuComponent::report_string_dp(uint8_t dp_id, const std::string &value) {
  this->report_dp(dp_id, TuyaDpType::STRING, reinterpret_cast<const uint8_t *>(value.data()),
                  static_cast<uint16_t>(value.size()));
}

void TuyaWifiMcuComponent::report_raw_dp(uint8_t dp_id, const std::vector<uint8_t> &value) {
  this->report_dp(dp_id, TuyaDpType::RAW, value.data(), static_cast<uint16_t>(value.size()));
}

void TuyaWifiMcuComponent::report_dp(uint8_t dp_id, TuyaDpType type, const uint8_t *value, uint16_t length) {
  const size_t unit_size = static_cast<size_t>(length) + 4;
  if (unit_size > MAX_TX_PAYLOAD_SIZE) {
    ESP_LOGW(TAG, "Tuya DP %u value too long (%u bytes)", dp_id, length);
    return;
  }
  if (this->batching_) {
    if (this->batch_.size() + unit_size > MAX_TX_PAYLOAD_SIZE) {
      this->flush_batch_();
    }
    this->batch_.add(dp_id, type, value, length);
    return;
  }
  TuyaDpBuilder builder;
  builder.add(dp_id, type, value, length);
  this->upload_dp_data(builder.data());
}

void TuyaWifiMcuComponent::upload_dp_data(const std::vector<uint8_t> &dp_data) {
  if (dp_data.empty() || dp_data.size() > MAX_TX_PAYLOAD_SIZE) {
    ESP_LOGW(TAG, "Ignoring DP upload of %u bytes", static_cast<unsigned>(dp_data.size()));
    return;
  }
  this->send_frame_(TuyaCommand::DP_UPLOAD, dp_data.data(), static_cast<uint16_t>(dp_data.size()));
}

void TuyaWifiMcuComponent::flush_batch_() {
  if (!this->batch_.empty()) {
    this->send_frame_(TuyaCommand::DP_UPLOAD, this->batch_.data().data(),
                      static_cast<uint16_t>(this->batch_.size()));
    this->batch_.clear();
  }
}

void TuyaWifiMcuComponent::report_tuya_dp_states() {
  // With batch_reports the per-entity reports are collected into aggregated
  // frames (Tuya recommends one packet for many DPs); otherwise each entity
  // sends its own frame exactly as in v1.4.0.
  this->batching_ = this->batch_reports_;
  for (auto *entity : this->entities_) {
    if (entity != nullptr) {
      entity->report_tuya_dp_state();
    }
  }
  if (this->batching_) {
    this->batching_ = false;
    this->flush_batch_();
  }
}

void TuyaWifiMcuComponent::upload_dp_data_sync(const std::vector<uint8_t> &dp_data) {
  if (dp_data.empty() || dp_data.size() > MAX_TX_PAYLOAD_SIZE) {
    ESP_LOGW(TAG, "Ignoring sync upload of %u bytes", static_cast<unsigned>(dp_data.size()));
    return;
  }
  this->enqueue_request_(TuyaCommand::DP_UPLOAD_SYNC, dp_data, TuyaCommand::DP_UPLOAD_SYNC_RESULT, -1,
                         UPLOAD_RESULT_TIMEOUT_MS, false, [this](bool ok, const uint8_t *data, uint16_t len) {
                           const bool success = ok && len >= 1 && data[0] == 0x01;
                           ESP_LOGD(TAG, "Sync upload %s", success ? "succeeded" : "failed");
                           for (auto &cb : this->upload_result_callbacks_)
                             cb(success, false);
                         });
}

void TuyaWifiMcuComponent::upload_dp_data_record(const std::vector<uint8_t> &dp_data, TuyaRecordTime mode,
                                                 const TuyaDateTime *time) {
  auto payload = build_record_payload(mode, time, dp_data);
  if (dp_data.empty() || payload.size() > MAX_TX_PAYLOAD_SIZE) {
    ESP_LOGW(TAG, "Ignoring record upload of %u bytes", static_cast<unsigned>(dp_data.size()));
    return;
  }
  this->enqueue_request_(TuyaCommand::EXTENDED_SERVICE, std::move(payload), TuyaCommand::EXTENDED_SERVICE,
                         static_cast<int16_t>(TuyaExtendedCommand::RECORD_UPLOAD), UPLOAD_RESULT_TIMEOUT_MS, false,
                         [this](bool ok, const uint8_t *data, uint16_t len) {
                           const bool success = ok && len >= 2 && data[1] == 0x00;
                           ESP_LOGD(TAG, "Record upload %s", success ? "succeeded" : "failed");
                           for (auto &cb : this->upload_result_callbacks_)
                             cb(success, true);
                         });
}

void TuyaWifiMcuComponent::reset_tuya_wifi() { this->send_frame_(TuyaCommand::WIFI_RESET); }

void TuyaWifiMcuComponent::handle_reset_button_(uint32_t now) {
  if (this->wifi_reset_pin_ == nullptr) {
    return;
  }

  const bool pressed = this->wifi_reset_pin_->digital_read();
  if (pressed != this->reset_raw_pressed_) {
    this->reset_raw_pressed_ = pressed;
    this->reset_transition_time_ = now;
  }

  if (pressed != this->reset_stable_pressed_ && now - this->reset_transition_time_ >= RESET_DEBOUNCE_MS) {
    this->reset_stable_pressed_ = pressed;
    if (pressed && !this->reset_press_handled_) {
      ESP_LOGD(TAG, "Starting Tuya smart config");
      this->send_frame_(TuyaCommand::WIFI_MODE, &SMART_CONFIG, 1);
      this->reset_press_handled_ = true;
    } else if (!pressed) {
      this->reset_press_handled_ = false;
    }
  }
}

void TuyaWifiMcuComponent::update_wifi_led_(uint32_t now) {
  if (this->wifi_led_pin_ == nullptr) {
    return;
  }

  if (this->wifi_work_state_ == WIFI_CONN_CLOUD) {
    if (!this->wifi_led_state_) {
      this->wifi_led_state_ = true;
      this->wifi_led_pin_->digital_write(true);
    }
    return;
  }

  if (this->wifi_work_state_ == WIFI_LOW_POWER || this->wifi_work_state_ == 0xFF) {
    if (this->wifi_led_state_) {
      this->wifi_led_state_ = false;
      this->wifi_led_pin_->digital_write(false);
    }
    return;
  }

  if (now - this->last_wifi_led_state_change_time_ >= WIFI_LED_BLINK_INTERVAL_MS) {
    this->last_wifi_led_state_change_time_ = now;
    this->wifi_led_state_ = !this->wifi_led_state_;
    this->wifi_led_pin_->digital_write(this->wifi_led_state_);
  }
}

// ---------------------------------------------------------------------------
// Request queue
// ---------------------------------------------------------------------------

void TuyaWifiMcuComponent::enqueue_request_(TuyaCommand command, std::vector<uint8_t> payload, TuyaCommand response,
                                           int16_t response_sub_command, uint32_t timeout_ms, bool coalesce,
                                           std::function<void(bool, const uint8_t *, uint16_t)> &&callback) {
  if (coalesce) {
    // A poll for the same data that has not been answered yet (queued or in
    // flight) simply gains another consumer instead of a second frame.
    for (auto &pending : this->requests_) {
      if (pending.coalesce && pending.command == command && pending.payload == payload) {
        if (callback)
          pending.callbacks.push_back(std::move(callback));
        return;
      }
    }
  }
  if (this->requests_.size() >= MAX_PENDING_REQUESTS) {
    ESP_LOGW(TAG, "Tuya request queue full, dropping command 0x%02X", static_cast<uint8_t>(command));
    if (callback)
      callback(false, nullptr, 0);
    return;
  }
  PendingRequest request{command, std::move(payload), response, response_sub_command, timeout_ms, coalesce, {}};
  if (callback)
    request.callbacks.push_back(std::move(callback));
  this->requests_.push_back(std::move(request));
}

void TuyaWifiMcuComponent::process_request_queue_(uint32_t now) {
  if (this->request_in_flight_) {
    if (now - this->request_sent_time_ < this->requests_.front().timeout_ms) {
      return;
    }
    PendingRequest expired = std::move(this->requests_.front());
    this->requests_.pop_front();
    this->request_in_flight_ = false;
    this->last_request_done_time_ = now;
    ESP_LOGD(TAG, "Tuya command 0x%02X timed out (module firmware may not support it)",
             static_cast<uint8_t>(expired.command));
    for (auto &cb : expired.callbacks)
      cb(false, nullptr, 0);
    return;
  }
  // Requests wait for the module's init handshake: commands sent earlier are
  // ignored by the module (Tuya: "must be sent after module initialization").
  if (this->requests_.empty() || !this->module_ready_ || now - this->last_request_done_time_ < REQUEST_GAP_MS) {
    return;
  }
  const auto &request = this->requests_.front();
  this->send_frame_(request.command, request.payload.empty() ? nullptr : request.payload.data(),
                    static_cast<uint16_t>(request.payload.size()));
  this->request_in_flight_ = true;
  this->request_sent_time_ = now;
}

bool TuyaWifiMcuComponent::match_pending_response_(const TuyaFrame &frame) {
  if (!this->request_in_flight_ || this->requests_.empty()) {
    return false;
  }
  const auto &request = this->requests_.front();
  if (frame.command != request.response) {
    return false;
  }
  if (request.response_sub_command >= 0 &&
      (frame.payload_length < 1 || frame.payload[0] != static_cast<uint8_t>(request.response_sub_command))) {
    return false;
  }
  PendingRequest done = std::move(this->requests_.front());
  this->requests_.pop_front();
  this->request_in_flight_ = false;
  this->last_request_done_time_ = millis();
  for (auto &cb : done.callbacks)
    cb(true, frame.payload, frame.payload_length);
  return true;
}

void TuyaWifiMcuComponent::mark_module_ready_() {
  if (this->module_ready_) {
    return;
  }
  this->module_ready_ = true;
  ESP_LOGD(TAG, "Tuya module initialised");
  if (this->reset_notification_) {
    this->enqueue_request_(TuyaCommand::EXTENDED_SERVICE, {static_cast<uint8_t>(TuyaExtendedCommand::RESET_NOTIFY_ENABLE)},
                           TuyaCommand::EXTENDED_SERVICE,
                           static_cast<int16_t>(TuyaExtendedCommand::RESET_NOTIFY_ENABLE), REQUEST_TIMEOUT_MS, true,
                           [](bool ok, const uint8_t *data, uint16_t len) {
                             if (ok && len >= 2 && data[1] == 0x00) {
                               ESP_LOGD(TAG, "Tuya reset notifications enabled");
                             } else {
                               ESP_LOGW(TAG, "Tuya module did not enable reset notifications");
                             }
                           });
  }
  for (auto &cb : this->module_ready_callbacks_)
    cb();
}

bool TuyaWifiMcuComponent::is_module_online() const {
  return this->rx_seen_ && millis() - this->last_rx_time_ < MODULE_OFFLINE_TIMEOUT_MS;
}

void TuyaWifiMcuComponent::set_network_status_(uint8_t status) {
  const bool was_cloud = this->is_cloud_connected();
  this->wifi_work_state_ = status;
  const bool cloud = this->is_cloud_connected();
  if (cloud && !was_cloud) {
    // Like the Tuya SDK, (re)enable the weather service once per cloud connection.
    this->weather_opened_ = false;
    this->open_weather();
  }
  for (auto &cb : this->network_status_callbacks_)
    cb(status);
}

// ---------------------------------------------------------------------------
// Module services
// ---------------------------------------------------------------------------

void TuyaWifiMcuComponent::request_time_(TuyaCommand command, TimeCallback &&callback) {
  this->enqueue_request_(command, {}, command, -1, REQUEST_TIMEOUT_MS, true,
                         [callback](bool ok, const uint8_t *data, uint16_t len) {
                           TuyaDateTime time;
                           const bool valid = ok && parse_tuya_time(data, len, time);
                           if (callback)
                             callback(valid, time);
                         });
}

void TuyaWifiMcuComponent::request_local_time(TimeCallback callback) {
  this->request_time_(TuyaCommand::GET_LOCAL_TIME, std::move(callback));
}

void TuyaWifiMcuComponent::request_gmt_time(TimeCallback callback) {
  this->request_time_(TuyaCommand::GET_GMT_TIME, std::move(callback));
}

void TuyaWifiMcuComponent::request_network_status() {
  this->enqueue_request_(TuyaCommand::GET_NETWORK_STATUS, {}, TuyaCommand::GET_NETWORK_STATUS, -1, REQUEST_TIMEOUT_MS,
                         true, [this](bool ok, const uint8_t *data, uint16_t len) {
                           if (ok && len >= 1 && data[0] <= 0x06) {
                             this->set_network_status_(data[0]);
                           }
                         });
}

void TuyaWifiMcuComponent::request_wifi_rssi() {
  this->enqueue_request_(TuyaCommand::GET_WIFI_RSSI, {}, TuyaCommand::GET_WIFI_RSSI, -1, REQUEST_TIMEOUT_MS, true,
                         [this](bool ok, const uint8_t *data, uint16_t len) {
                           // 0x00 means failure; a negative byte is the RSSI in dBm.
                           const int8_t rssi = (ok && len >= 1) ? static_cast<int8_t>(data[0]) : 0;
                           const bool success = rssi < 0;
                           for (auto &cb : this->rssi_callbacks_)
                             cb(success, rssi);
                         });
}

void TuyaWifiMcuComponent::request_mac() {
  this->enqueue_request_(TuyaCommand::GET_MAC, {}, TuyaCommand::GET_MAC, -1, REQUEST_TIMEOUT_MS, true,
                         [this](bool ok, const uint8_t *data, uint16_t len) {
                           // Data[0]: 0x00 = success, followed by 6 MAC bytes.
                           if (!ok || len < 7 || data[0] != 0x00) {
                             return;
                           }
                           const std::string mac = tuya_format_mac(data + 1);
                           for (auto &cb : this->mac_callbacks_)
                             cb(mac);
                         });
}

void TuyaWifiMcuComponent::request_free_memory() {
  this->enqueue_request_(TuyaCommand::GET_FREE_MEMORY, {}, TuyaCommand::GET_FREE_MEMORY, -1, REQUEST_TIMEOUT_MS,
                         true, [this](bool ok, const uint8_t *data, uint16_t len) {
                           if (!ok || len < 4) {
                             return;
                           }
                           const uint32_t free_memory = decode_tuya_value(data);
                           for (auto &cb : this->free_memory_callbacks_)
                             cb(free_memory);
                         });
}

void TuyaWifiMcuComponent::start_wifi_test() {
  // The module scans for the "tuya_mdev_test" SSID; this takes a few seconds.
  this->enqueue_request_(TuyaCommand::WIFI_TEST_SCAN, {}, TuyaCommand::WIFI_TEST_SCAN, -1, 10000, true,
                         [this](bool ok, const uint8_t *data, uint16_t len) {
                           // Data[0] = 0x01: Data[1] is signal strength 0-100.
                           // Data[0] = 0x00: Data[1] is an error code (0 = SSID not found, 1 = no license).
                           const bool success = ok && len >= 2 && data[0] == 0x01;
                           const uint8_t value = (ok && len >= 2) ? data[1] : 0;
                           ESP_LOGD(TAG, "WiFi test %s (%u)", success ? "passed" : "failed", value);
                           for (auto &cb : this->wifi_test_callbacks_)
                             cb(success, value);
                         });
}

void TuyaWifiMcuComponent::add_weather_key(const std::string &key) {
  for (const auto &existing : this->weather_keys_) {
    if (existing == key)
      return;
  }
  this->weather_keys_.push_back(key);
}

void TuyaWifiMcuComponent::open_weather() {
  if (this->weather_keys_.empty() || this->weather_opened_) {
    return;
  }
  std::vector<std::string> keys = this->weather_keys_;
  bool time_dependent = false;
  for (const auto &key : keys) {
    if (key.find("sunrise") != std::string::npos || key.find("sunset") != std::string::npos ||
        key.find("sunRise") != std::string::npos || key.find("sunSet") != std::string::npos) {
      time_dependent = true;
    }
  }
  if (time_dependent && !this->weather_time_key_.empty()) {
    keys.push_back(this->weather_time_key_);
  }
  keys.push_back("w.date." + std::to_string(this->weather_forecast_days_));
  this->enqueue_request_(TuyaCommand::WEATHER_OPEN, build_weather_open_payload(keys), TuyaCommand::WEATHER_OPEN, -1,
                         REQUEST_TIMEOUT_MS, true, [this](bool ok, const uint8_t *data, uint16_t len) {
                           // Data[0]: 0x01 success, 0x00 failure with Data[1] as error code.
                           this->weather_opened_ = ok && len >= 1 && data[0] == 0x01;
                           if (this->weather_opened_) {
                             ESP_LOGD(TAG, "Tuya weather service enabled");
                           } else {
                             ESP_LOGW(TAG, "Tuya weather service not enabled (code %u)",
                                      (ok && len >= 2) ? data[1] : 0xFF);
                           }
                         });
}

void TuyaWifiMcuComponent::request_weather() {
  if (!this->weather_opened_) {
    this->open_weather();
    return;
  }
  // Module-side rate limit: one request per 20 minutes; data arrives as 0x21.
  this->enqueue_request_(TuyaCommand::EXTENDED_SERVICE, {static_cast<uint8_t>(TuyaExtendedCommand::WEATHER_REQUEST)},
                         TuyaCommand::EXTENDED_SERVICE, static_cast<int16_t>(TuyaExtendedCommand::WEATHER_REQUEST),
                         REQUEST_TIMEOUT_MS, true, nullptr);
}

void TuyaWifiMcuComponent::start_pairing(TuyaPairingMode mode) {
  const uint8_t value = static_cast<uint8_t>(mode);
  this->send_frame_(TuyaCommand::WIFI_MODE, &value, 1);
}

void TuyaWifiMcuComponent::handle_weather_data_(const TuyaFrame &frame) {
  std::vector<TuyaWeatherItem> items;
  if (!parse_tuya_weather(frame.payload, frame.payload_length, items)) {
    ESP_LOGW(TAG, "Tuya weather data rejected or malformed");
  }
  for (const auto &item : items) {
    ESP_LOGV(TAG, "Weather %s[%u] = %s", item.key.c_str(), item.day, item.string_value.c_str());
    for (auto &cb : this->weather_callbacks_)
      cb(item);
  }
  // The weather acknowledgement uses version 0x00 per Tuya's reference SDK.
  this->send_frame_with_version_(0x00, TuyaCommand::WEATHER_DATA, nullptr, 0);
}

void TuyaWifiMcuComponent::handle_extended_service_(const TuyaFrame &frame) {
  if (frame.payload_length < 1) {
    return;
  }
  const auto sub_command = static_cast<TuyaExtendedCommand>(frame.payload[0]);
  if (sub_command == TuyaExtendedCommand::RESET_NOTIFY && frame.payload_length >= 2) {
    const uint8_t reason = frame.payload[1];  // 0 local, 1 app, 2 app factory reset
    ESP_LOGI(TAG, "Tuya module reset notification (reason %u)", reason);
    const uint8_t ack = static_cast<uint8_t>(TuyaExtendedCommand::RESET_NOTIFY);
    this->send_frame_(TuyaCommand::EXTENDED_SERVICE, &ack, 1);
    for (auto &cb : this->module_reset_callbacks_)
      cb(reason);
    return;
  }
  ESP_LOGD(TAG, "Unsolicited Tuya extended service 0x%02X", frame.payload[0]);
}

float TuyaWifiMcuComponent::get_setup_priority() const { return setup_priority::AFTER_WIFI; }

}  // namespace tuya_wifi_mcu
}  // namespace esphome
