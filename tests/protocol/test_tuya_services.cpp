// Host unit tests for the framework-independent helpers in tuya_services.cpp.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../../components/tuya_wifi_mcu/tuya_services.h"

using namespace esphome::tuya_wifi_mcu;

static void test_builder_all_types() {
  TuyaDpBuilder b;
  b.add_bool(1, true).add_value(2, -123).add_enum(3, 2).add_bitmap(4, 0x0102, 2).add_string(5, "ab");
  const uint8_t raw[] = {0xDE, 0xAD};
  b.add_raw(6, raw, 2);
  const std::vector<uint8_t> expected = {
      1, 0x01, 0, 1, 1,                          // bool
      2, 0x02, 0, 4, 0xFF, 0xFF, 0xFF, 0x85,     // value -123, big-endian two's complement
      3, 0x04, 0, 1, 2,                          // enum
      4, 0x05, 0, 2, 0x01, 0x02,                 // bitmap, 2 bytes
      5, 0x03, 0, 2, 'a', 'b',                   // string
      6, 0x00, 0, 2, 0xDE, 0xAD,                 // raw
  };
  assert(b.data() == expected);

  TuyaDpBuilder odd;
  odd.add_bitmap(7, 0x01020304, 3);  // invalid width coerced to 4
  assert((odd.data() == std::vector<uint8_t>{7, 0x05, 0, 4, 1, 2, 3, 4}));
}

static void test_time_parsing() {
  TuyaDateTime t;
  // 0x0C GMT example from the Tuya documentation: 2016-04-19 05:06:07.
  const uint8_t gmt[] = {0x01, 0x10, 0x04, 0x13, 0x05, 0x06, 0x07};
  assert(parse_tuya_time(gmt, sizeof(gmt), t));
  assert(t.year == 2016 && t.month == 4 && t.day == 19 && t.hour == 5 && t.minute == 6 && t.second == 7);
  assert(t.weekday == 0);

  const uint8_t local[] = {0x01, 26, 10, 29, 23, 59, 58, 4};
  assert(parse_tuya_time(local, sizeof(local), t));
  assert(t.year == 2026 && t.weekday == 4);

  const uint8_t not_synced[] = {0x00, 26, 10, 29, 23, 59, 58, 4};
  assert(!parse_tuya_time(not_synced, sizeof(not_synced), t));
  const uint8_t bad_month[] = {0x01, 26, 13, 29, 23, 59, 58};
  assert(!parse_tuya_time(bad_month, sizeof(bad_month), t));
  assert(!parse_tuya_time(local, 6, t));
}

static std::vector<uint8_t> item(const std::string &key, bool is_string, const std::vector<uint8_t> &value) {
  std::vector<uint8_t> out = {static_cast<uint8_t>(key.size())};
  out.insert(out.end(), key.begin(), key.end());
  out.push_back(is_string ? 0x01 : 0x00);
  out.push_back(static_cast<uint8_t>(value.size()));
  out.insert(out.end(), value.begin(), value.end());
  return out;
}

static void test_weather_parsing() {
  // Documentation example: w.temp 15, w.humidity 23, w.conditionNum "120".
  std::vector<uint8_t> payload = {0x01};
  for (const auto &part : {item("w.temp", false, {0, 0, 0, 15}), item("w.humidity", false, {0, 0, 0, 23}),
                           item("w.conditionNum", true, {'1', '2', '0'}), item("w.temp.1", false, {0xFF, 0xFF, 0xFF, 0xFE})}) {
    payload.insert(payload.end(), part.begin(), part.end());
  }
  std::vector<TuyaWeatherItem> items;
  assert(parse_tuya_weather(payload.data(), payload.size(), items));
  assert(items.size() == 4);
  assert(items[0].key == "w.temp" && items[0].day == 0 && items[0].int_value == 15 && !items[0].is_string);
  assert(items[1].key == "w.humidity" && items[1].string_value == "23");
  assert(items[2].key == "w.conditionNum" && items[2].is_string && items[2].string_value == "120");
  // Forecast-day suffix is split off; negative integers are preserved.
  assert(items[3].key == "w.temp" && items[3].day == 1 && items[3].int_value == -2);

  std::vector<TuyaWeatherItem> none;
  const uint8_t denied[] = {0x00};
  assert(!parse_tuya_weather(denied, 1, none) && none.empty());

  std::vector<uint8_t> truncated = payload;
  truncated.resize(truncated.size() - 2);
  std::vector<TuyaWeatherItem> partial;
  assert(!parse_tuya_weather(truncated.data(), truncated.size(), partial));
  assert(partial.size() == 3);  // units before the damage are kept

  const auto open = build_weather_open_payload({"w.temp", "w.pm25"});
  // Documentation example payload for w.temp + w.pm25.
  const std::vector<uint8_t> expected = {0x06, 'w', '.', 't', 'e', 'm', 'p', 0x06, 'w', '.', 'p', 'm', '2', '5'};
  assert(open == expected);
}

static void test_record_payload() {
  TuyaDpBuilder dp;
  dp.add_bool(1, true);
  // Documentation example: GMT 2022-02-18 16:27:06, DP 1 = true.
  TuyaDateTime t;
  t.year = 2022;
  t.month = 2;
  t.day = 18;
  t.hour = 16;
  t.minute = 27;
  t.second = 6;
  const auto gmt = build_record_payload(TuyaRecordTime::GMT, &t, dp.data());
  const std::vector<uint8_t> expected = {0x0B, 0x01, 0x02, 0x16, 0x02, 0x12, 0x10, 0x1B, 0x06, 0x01, 0x01, 0x00, 0x01, 0x01};
  assert(gmt == expected);

  const auto module = build_record_payload(TuyaRecordTime::MODULE, nullptr, dp.data());
  assert((module == std::vector<uint8_t>{0x0B, 0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01}));
  // A missing time falls back to the module clock instead of sending garbage.
  assert(build_record_payload(TuyaRecordTime::LOCAL, nullptr, dp.data()) == module);
}

static void test_formatting_helpers() {
  std::vector<uint8_t> bytes;
  assert(tuya_parse_hex("0a:1B ff", bytes));
  assert((bytes == std::vector<uint8_t>{0x0A, 0x1B, 0xFF}));
  assert(tuya_format_hex(bytes) == "0a1bff");
  assert(!tuya_parse_hex("abc", bytes));
  assert(!tuya_parse_hex("zz", bytes));

  const uint8_t mac[] = {0x50, 0x8A, 0x06, 0xE3, 0xA2, 0xD9};
  assert(tuya_format_mac(mac) == "50:8A:06:E3:A2:D9");

  assert(std::string(tuya_network_status_to_string(0x04)) == "Connected to cloud");
  assert(std::string(tuya_network_status_to_string(0x09)) == "Unknown");

  assert(tuya_dp_as_int(0x02, {0xFF, 0xFF, 0xFF, 0xFF}) == -1);
  assert(tuya_dp_as_int(0x05, {0x01, 0x00}) == 256);
  assert(tuya_dp_as_int(0x04, {7}) == 7);
  assert(tuya_dp_as_string(0x03, {'h', 'i'}) == "hi");
  assert(tuya_dp_as_string(0x00, {0x0A}) == "0a");
  assert(tuya_dp_as_string(0x01, {1}) == "1");
}

int main() {
  test_builder_all_types();
  test_time_parsing();
  test_weather_parsing();
  test_record_payload();
  test_formatting_helpers();
  std::puts("tuya services tests passed");
  return 0;
}
