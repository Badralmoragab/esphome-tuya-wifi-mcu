#pragma once

#include <cmath>
#include <cstdint>

namespace esphome {
namespace tuya_wifi_mcu {

/// Tuya VALUE DPs are integers with a decimal "scale" defined on the Tuya
/// platform: the real value is raw / 10^scale (scale 1 -> 23.5 sent as 235).
inline float tuya_scale_factor(uint8_t scale) {
  float factor = 1.0f;
  for (uint8_t i = 0; i < scale; i++) {
    factor *= 10.0f;
  }
  return factor;
}

inline int32_t tuya_to_raw(float value, uint8_t scale) {
  return static_cast<int32_t>(std::lround(value * tuya_scale_factor(scale)));
}

inline float tuya_from_raw(int32_t raw, uint8_t scale) { return static_cast<float>(raw) / tuya_scale_factor(scale); }

}  // namespace tuya_wifi_mcu
}  // namespace esphome
