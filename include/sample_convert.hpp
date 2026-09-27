#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dsp4 {

inline float pcm_s32_to_float(std::int32_t value) {
    return static_cast<float>(value) * (1.0f / 2147483648.0f);
}

inline std::int32_t float_to_pcm_s32(float value) {
    if (!std::isfinite(value)) return 0;
    if (value >= 1.0f) return INT32_MAX;
    if (value <= -1.0f) return INT32_MIN;
    // RP2350 converts float to integer in hardware. At 32-bit output, simple
    // truncation is far below the converter noise and avoids an llround call
    // for each of eight output samples.
    return static_cast<std::int32_t>(value * 2147483648.0f);
}

inline float pcm_s24_left_justified_to_float(std::int32_t value) {
    return static_cast<float>(value) * (1.0f / 2147483648.0f);
}

}  // namespace dsp4
