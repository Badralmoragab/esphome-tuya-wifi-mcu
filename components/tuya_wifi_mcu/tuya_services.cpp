#include "tuya_services.h"

#include <cstdio>

namespace esphome {
namespace tuya_wifi_mcu {

// ---------------------------------------------------------------------------
// TuyaDpBuilder
// ---------------------------------------------------------------------------

TuyaDpBuilder &TuyaDpBuilder::add(uint8_t dp_id, TuyaDpType type, const uint8_t *value, uint16_t length) {
  this->data_.push_back(dp_id);
  this->data_.push_back(static_cast<uint8_t>(type));
  this->data_.push_back(static_cast<uint8_t>(length >> 8));
  this->data_.push_back(static_cast<uint8_t>(length & 0xFF));
  if (value != nullptr && length != 0) {
    this->data_.insert(this->data_.end(), value, value + length);
  }
  return *this;
}

TuyaDpBuilder &TuyaDpBuilder::add_bool(uint8_t dp_id, bool value) {
  const uint8_t byte = value ? 0x01 : 0x00;
  return this->add(dp_id, TuyaDpType::BOOLEAN, &byte, 1);
}

TuyaDpBuilder &TuyaDpBuilder::add_value(uint8_t dp_id, int32_t value) {
  uint8_t bytes[4];
  encode_tuya_value(static_cast<uint32_t>(value), bytes);
  return this->add(dp_id, TuyaDpType::VALUE, bytes, 4);
}

TuyaDpBuilder &TuyaDpBuilder::add_enum(uint8_t dp_id, uint8_t value) {
  return this->add(dp_id, TuyaDpType::ENUM, &value, 1);
}

TuyaDpBuilder &TuyaDpBuilder::add_bitmap(uint8_t dp_id, uint32_t value, uint8_t width) {
  if (width != 1 && width != 2) {
    width = 4;
  }
  uint8_t bytes[4];
  for (uint8_t i = 0; i < width; i++) {
    bytes[i] = static_cast<uint8_t>(value >> (8 * (width - 1 - i)));
  }
  return this->add(dp_id, TuyaDpType::BITMAP, bytes, width);
}

TuyaDpBuilder &TuyaDpBuilder::add_string(uint8_t dp_id, const std::string &value) {
  return this->add(dp_id, TuyaDpType::STRING, reinterpret_cast<const uint8_t *>(value.data()),
                   static_cast<uint16_t>(value.size()));
}

TuyaDpBuilder &TuyaDpBuilder::add_raw(uint8_t dp_id, const uint8_t *value, uint16_t length) {
  return this->add(dp_id, TuyaDpType::RAW, value, length);
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

bool parse_tuya_time(const uint8_t *payload, uint16_t length, TuyaDateTime &out) {
  if (payload == nullptr || length < 7 || payload[0] != 0x01) {
    return false;
  }
  TuyaDateTime t;
  t.year = static_cast<uint16_t>(2000 + payload[1]);
  t.month = payload[2];
  t.day = payload[3];
  t.hour = payload[4];
  t.minute = payload[5];
  t.second = payload[6];
  t.weekday = length >= 8 ? payload[7] : 0;
  if (t.month < 1 || t.month > 12 || t.day < 1 || t.day > 31 || t.hour > 23 || t.minute > 59 || t.second > 59 ||
      t.weekday > 7) {
    return false;
  }
  out = t;
  return true;
}

// ---------------------------------------------------------------------------
// Weather
// ---------------------------------------------------------------------------

// Splits a trailing ".N" forecast-day suffix ("w.temp.1" -> "w.temp", 1).
// Parameter names themselves never end in ".<digits>", except the request-only
// "w.date.N" which the module does not echo back as data.
static void split_day_suffix(std::string &key, uint8_t &day) {
  day = 0;
  const size_t dot = key.rfind('.');
  if (dot == std::string::npos || dot + 1 >= key.size() || dot < 2) {
    return;
  }
  uint32_t value = 0;
  for (size_t i = dot + 1; i < key.size(); i++) {
    if (key[i] < '0' || key[i] > '9') {
      return;
    }
    value = value * 10 + static_cast<uint32_t>(key[i] - '0');
    if (value > 31) {
      return;
    }
  }
  day = static_cast<uint8_t>(value);
  key.resize(dot);
}

bool parse_tuya_weather(const uint8_t *payload, uint16_t length, std::vector<TuyaWeatherItem> &items) {
  if (payload == nullptr || length < 1 || payload[0] != 0x01) {
    return false;
  }
  uint16_t offset = 1;
  while (offset < length) {
    const uint8_t key_length = payload[offset++];
    if (key_length == 0 || length - offset < key_length + 2) {
      return false;
    }
    TuyaWeatherItem item;
    item.key.assign(reinterpret_cast<const char *>(payload + offset), key_length);
    offset += key_length;
    const uint8_t type = payload[offset++];
    const uint8_t value_length = payload[offset++];
    if (length - offset < value_length || (type != 0x00 && type != 0x01)) {
      return false;
    }
    split_day_suffix(item.key, item.day);
    if (type == 0x00) {
      if (value_length != 4) {
        return false;
      }
      item.int_value = static_cast<int32_t>(decode_tuya_value(payload + offset));
      item.string_value = std::to_string(item.int_value);
    } else {
      item.is_string = true;
      item.string_value.assign(reinterpret_cast<const char *>(payload + offset), value_length);
    }
    offset += value_length;
    items.push_back(std::move(item));
  }
  return true;
}

std::vector<uint8_t> build_weather_open_payload(const std::vector<std::string> &keys) {
  std::vector<uint8_t> payload;
  for (const auto &key : keys) {
    if (key.empty() || key.size() > 255) {
      continue;
    }
    payload.push_back(static_cast<uint8_t>(key.size()));
    payload.insert(payload.end(), key.begin(), key.end());
  }
  return payload;
}

// ---------------------------------------------------------------------------
// Record-type upload
// ---------------------------------------------------------------------------

std::vector<uint8_t> build_record_payload(TuyaRecordTime mode, const TuyaDateTime *time,
                                          const std::vector<uint8_t> &dp_data) {
  std::vector<uint8_t> payload = {static_cast<uint8_t>(TuyaExtendedCommand::RECORD_UPLOAD), 0x01};
  if (mode == TuyaRecordTime::MODULE || time == nullptr) {
    payload.push_back(static_cast<uint8_t>(TuyaRecordTime::MODULE));
  } else {
    payload.push_back(static_cast<uint8_t>(mode));
    payload.push_back(static_cast<uint8_t>(time->year >= 2000 ? time->year - 2000 : 0));
    payload.push_back(time->month);
    payload.push_back(time->day);
    payload.push_back(time->hour);
    payload.push_back(time->minute);
    payload.push_back(time->second);
  }
  payload.insert(payload.end(), dp_data.begin(), dp_data.end());
  return payload;
}

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------

const char *tuya_network_status_to_string(uint8_t status) {
  switch (status) {
    case 0x00:
      return "Pairing (EZ)";
    case 0x01:
      return "Pairing (AP)";
    case 0x02:
      return "WiFi configured, not connected";
    case 0x03:
      return "Connected to router";
    case 0x04:
      return "Connected to cloud";
    case 0x05:
      return "Low power";
    case 0x06:
      return "Pairing (EZ+AP)";
    default:
      return "Unknown";
  }
}

std::string tuya_format_hex(const uint8_t *data, size_t length) {
  static const char *const DIGITS = "0123456789abcdef";
  std::string out;
  out.reserve(length * 2);
  for (size_t i = 0; i < length; i++) {
    out.push_back(DIGITS[data[i] >> 4]);
    out.push_back(DIGITS[data[i] & 0x0F]);
  }
  return out;
}

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

bool tuya_parse_hex(const std::string &text, std::vector<uint8_t> &out) {
  std::vector<uint8_t> result;
  int high = -1;
  for (char c : text) {
    if (c == ' ' || c == ':' || c == '-') {
      continue;
    }
    const int nibble = hex_nibble(c);
    if (nibble < 0) {
      return false;
    }
    if (high < 0) {
      high = nibble;
    } else {
      result.push_back(static_cast<uint8_t>((high << 4) | nibble));
      high = -1;
    }
  }
  if (high >= 0) {
    return false;
  }
  out = std::move(result);
  return true;
}

std::string tuya_format_mac(const uint8_t *mac) {
  char buffer[18];
  snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
           mac[5]);
  return buffer;
}

int32_t tuya_dp_as_int(uint8_t type, const std::vector<uint8_t> &data) {
  switch (static_cast<TuyaDpType>(type)) {
    case TuyaDpType::BOOLEAN:
    case TuyaDpType::ENUM:
      return data.size() == 1 ? data[0] : 0;
    case TuyaDpType::VALUE:
      return data.size() == 4 ? static_cast<int32_t>(decode_tuya_value(data.data())) : 0;
    case TuyaDpType::BITMAP: {
      if (data.size() != 1 && data.size() != 2 && data.size() != 4) {
        return 0;
      }
      uint32_t value = 0;
      for (uint8_t byte : data) {
        value = (value << 8) | byte;
      }
      return static_cast<int32_t>(value);
    }
    default:
      return 0;
  }
}

std::string tuya_dp_as_string(uint8_t type, const std::vector<uint8_t> &data) {
  switch (static_cast<TuyaDpType>(type)) {
    case TuyaDpType::STRING:
      return std::string(data.begin(), data.end());
    case TuyaDpType::RAW:
      return tuya_format_hex(data);
    default:
      return std::to_string(tuya_dp_as_int(type, data));
  }
}

}  // namespace tuya_wifi_mcu
}  // namespace esphome
