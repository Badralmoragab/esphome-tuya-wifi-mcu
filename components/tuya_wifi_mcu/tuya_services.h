#pragma once

// Framework-independent helpers for the extended Tuya MCU protocol features.
//
// Architectural note: everything in this file is plain C++17 with no ESPHome
// dependency, so it is compiled into the host unit tests (tests/protocol) as
// well as the firmware. The component only glues these helpers to UART I/O
// and ESPHome entities; all byte-level encoding/decoding lives here.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "tuya_protocol.h"

namespace esphome {
namespace tuya_wifi_mcu {

/// Incrementally builds a Tuya DP data set: one or more
/// [dp_id][type][len_hi][len_lo][value...] units, as used by DP_UPLOAD (0x07),
/// DP_UPLOAD_SYNC (0x22) and the record-type upload (0x34 0x0B).
/// Multi-byte values are big-endian as required by the protocol.
class TuyaDpBuilder {
 public:
  TuyaDpBuilder &add(uint8_t dp_id, TuyaDpType type, const uint8_t *value, uint16_t length);
  TuyaDpBuilder &add_bool(uint8_t dp_id, bool value);
  TuyaDpBuilder &add_value(uint8_t dp_id, int32_t value);
  TuyaDpBuilder &add_enum(uint8_t dp_id, uint8_t value);
  /// width must be 1, 2 or 4 bytes; any other width is coerced to 4.
  TuyaDpBuilder &add_bitmap(uint8_t dp_id, uint32_t value, uint8_t width);
  TuyaDpBuilder &add_string(uint8_t dp_id, const std::string &value);
  TuyaDpBuilder &add_raw(uint8_t dp_id, const uint8_t *value, uint16_t length);
  TuyaDpBuilder &add_raw(uint8_t dp_id, const std::vector<uint8_t> &value) {
    return this->add_raw(dp_id, value.data(), static_cast<uint16_t>(value.size()));
  }

  const std::vector<uint8_t> &data() const { return this->data_; }
  size_t size() const { return this->data_.size(); }
  bool empty() const { return this->data_.empty(); }
  void clear() { this->data_.clear(); }

 protected:
  std::vector<uint8_t> data_;
};

/// Date/time as carried by GET_GMT_TIME (0x0C), GET_LOCAL_TIME (0x1C) and the
/// record-type upload. weekday is 1 = Monday ... 7 = Sunday, 0 when absent.
struct TuyaDateTime {
  uint16_t year{0};
  uint8_t month{0};
  uint8_t day{0};
  uint8_t hour{0};
  uint8_t minute{0};
  uint8_t second{0};
  uint8_t weekday{0};
};

/// Parses a 0x0C / 0x1C response: [ok][yy][mm][dd][hh][mi][ss](+[weekday]).
/// Returns false when the module reports failure (time not yet synced),
/// the payload is too short, or any field is out of range.
bool parse_tuya_time(const uint8_t *payload, uint16_t length, TuyaDateTime &out);

/// One weather parameter from a WEATHER_DATA (0x21) frame.
struct TuyaWeatherItem {
  std::string key;      ///< Parameter name without a forecast-day suffix, e.g. "w.temp".
  uint8_t day{0};       ///< Forecast day (0 = today) taken from a ".N" suffix, if present.
  bool is_string{false};
  int32_t int_value{0};
  std::string string_value;  ///< Always filled: decimal text for integer items.
};

/// Parses a WEATHER_DATA payload: [result] then repeated [L][K][T][VL][V].
/// Returns false when the result byte is not success or the payload is
/// malformed; items parsed before a malformed unit are kept.
bool parse_tuya_weather(const uint8_t *payload, uint16_t length, std::vector<TuyaWeatherItem> &items);

/// Builds a WEATHER_OPEN (0x20) payload: repeated [L][K] parameter names.
std::vector<uint8_t> build_weather_open_payload(const std::vector<std::string> &keys);

/// Time source for the record-type upload (0x34 0x0B).
enum class TuyaRecordTime : uint8_t {
  MODULE = 0x00,  ///< Module stamps the record with its own synced clock (no time bytes sent).
  LOCAL = 0x01,   ///< MCU supplies local time.
  GMT = 0x02,     ///< MCU supplies GMT.
};

/// Builds a record-type upload payload: [0x0B][0x01][mode](+6 time bytes)[DP units].
/// time is ignored for TuyaRecordTime::MODULE and required otherwise.
std::vector<uint8_t> build_record_payload(TuyaRecordTime mode, const TuyaDateTime *time,
                                          const std::vector<uint8_t> &dp_data);

/// Human-readable network status for 0x03 / 0x2B values 0x00-0x06.
const char *tuya_network_status_to_string(uint8_t status);

/// Lower-case hex without separators ("0a1bff"). Used for RAW DPs in text form.
std::string tuya_format_hex(const uint8_t *data, size_t length);
inline std::string tuya_format_hex(const std::vector<uint8_t> &data) {
  return tuya_format_hex(data.data(), data.size());
}
/// Parses hex text; spaces, ':' and '-' separators are ignored. Returns false
/// on an odd digit count or a non-hex character.
bool tuya_parse_hex(const std::string &text, std::vector<uint8_t> &out);

/// "AA:BB:CC:DD:EE:FF" from 6 bytes.
std::string tuya_format_mac(const uint8_t *mac);

/// Interprets a downloaded DP value as an integer: BOOLEAN/ENUM (1 byte),
/// VALUE (4-byte signed big-endian) and BITMAP (1/2/4 bytes). Returns 0 for
/// RAW/STRING or invalid lengths. Convenient inside on_dp_download lambdas.
int32_t tuya_dp_as_int(uint8_t type, const std::vector<uint8_t> &data);
/// Interprets a downloaded DP value as text (STRING as-is, RAW as hex,
/// numeric types as decimal).
std::string tuya_dp_as_string(uint8_t type, const std::vector<uint8_t> &data);

}  // namespace tuya_wifi_mcu
}  // namespace esphome
