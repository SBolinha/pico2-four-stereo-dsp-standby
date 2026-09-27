#pragma once

#include <array>
#include <cstddef>

namespace dsp4 {

struct BiquadCoeffs {
    float b0, b1, b2, a1, a2;
};

struct BandDefinition {
    const BiquadCoeffs* sections;
    std::size_t section_count;
    float signed_gain;
    std::size_t delay_samples;
    const char* id;
};

struct StereoFrame {
    float left;
    float right;
};

using OutputFrame = std::array<float, 8>;

}  // namespace dsp4
