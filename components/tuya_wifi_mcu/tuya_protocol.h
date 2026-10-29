#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome {
namespace tuya_wifi_mcu {

enum class TuyaCommand : uint8_t {
  HEARTBEAT = 0x00,
  PRODUCT_QUERY = 0x01,
  WORK_MODE_QUERY = 0x02,
  WIFI_STATE = 0x03,
  WIFI_RESET = 0x04,
  WIFI_MODE = 0x05,
  DP_DOWNLOAD = 0x06,
  DP_UPLOAD = 0x07,
  STATE_QUERY = 0x08,
  // Extended commands (Tuya MCU standard protocol). All are additive: the
  // original 0x00-0x08 values above are unchanged.
  GET_GMT_TIME = 0x0C,
  WIFI_TEST_SCAN = 0x0E,
  GET_FREE_MEMORY = 0x0F,
  GET_LOCAL_TIME = 0x1C,
  WEATHER_OPEN = 0x20,
  WEATHER_DATA = 0x21,
  DP_UPLOAD_SYNC = 0x22,
  DP_UPLOAD_SYNC_RESULT = 0x23,
  GET_WIFI_RSSI = 0x24,
  GET_NETWORK_STATUS = 0x2B,
  GET_MAC = 0x2D,
  EXTENDED_SERVICE = 0x34,
  NEW_FEATURE_CONFIG = 0x37,
};

/// Sub-commands carried in the first payload byte of EXTENDED_SERVICE (0x34)
/// and NEW_FEATURE_CONFIG (0x37) frames.
enum class TuyaExtendedCommand : uint8_t {
  WEATHER_REQUEST = 0x03,
  RESET_NOTIFY_ENABLE = 0x04,
  RESET_NOTIFY = 0x05,
  RECORD_UPLOAD = 0x0B,
};

/// Protocol version byte used by the MCU (this firmware) for every frame it
/// originates. The weather-data acknowledgement is the single documented
/// exception and is sent with version 0x00 (see WEATHER_DATA handling).
static constexpr uint8_t TUYA_MCU_FRAME_VERSION = 0x03;

enum class TuyaDpType : uint8_t {
  RAW = 0x00,
  BOOLEAN = 0x01,
  VALUE = 0x02,
  STRING = 0x03,
  ENUM = 0x04,
  BITMAP = 0x05,
};

struct TuyaFrame {
  uint8_t version;
  TuyaCommand command;
  const uint8_t *payload;
  uint16_t payload_length;
};

class TuyaProtocolListener {
 public:
  virtual void on_tuya_frame(const TuyaFrame &frame) = 0;
};

class TuyaProtocolParser {
 public:
  static constexpr uint16_t MAX_PAYLOAD_SIZE = 1024;
  static constexpr uint32_t INTER_BYTE_TIMEOUT_MS = 100;

  explicit TuyaProtocolParser(TuyaProtocolListener *listener) : listener_(listener) {}

  void feed(uint8_t byte, uint32_t now);
  void check_timeout(uint32_t now);
  void reset();

  uint32_t get_checksum_errors() const { return this->checksum_errors_; }
  uint32_t get_version_errors() const { return this->version_errors_; }
  uint32_t get_oversize_errors() const { return this->oversize_errors_; }
  uint32_t get_timeout_errors() const { return this->timeout_errors_; }

 protected:
  enum class State : uint8_t {
    WAIT_HEADER_55,
    WAIT_HEADER_AA,
    READ_VERSION,
    READ_COMMAND,
    READ_LENGTH_HIGH,
    READ_LENGTH_LOW,
    READ_PAYLOAD,
    READ_CHECKSUM,
    SKIP_OVERSIZE,
  };

  void start_frame_(uint32_t now);
  void reset_with_byte_(uint8_t byte, uint32_t now);

  TuyaProtocolListener *listener_;
  State state_{State::WAIT_HEADER_55};
  std::array<uint8_t, MAX_PAYLOAD_SIZE> payload_{};
  uint16_t payload_length_{0};
  uint16_t payload_position_{0};
  uint32_t skip_remaining_{0};
  uint8_t version_{0};
  uint8_t command_{0};
  uint8_t checksum_{0};
  uint32_t last_byte_time_{0};
  bool frame_active_{false};
  uint32_t checksum_errors_{0};
  uint32_t version_errors_{0};
  uint32_t oversize_errors_{0};
  uint32_t timeout_errors_{0};
};

size_t encode_tuya_frame(TuyaCommand command, const uint8_t *payload, uint16_t payload_length, uint8_t *output,
                         size_t output_capacity);
/// Same as encode_tuya_frame() but with an explicit protocol version byte.
size_t encode_tuya_frame_with_version(uint8_t version, TuyaCommand command, const uint8_t *payload,
                                      uint16_t payload_length, uint8_t *output, size_t output_capacity);

uint32_t decode_tuya_value(const uint8_t *data);
void encode_tuya_value(uint32_t value, uint8_t *data);

}  // namespace tuya_wifi_mcu
}  // namespace esphome
