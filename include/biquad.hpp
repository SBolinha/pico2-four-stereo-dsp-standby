#pragma once

#include "dsp_types.hpp"

namespace dsp4 {

struct BiquadState {
    float z1{0.0f};
    float z2{0.0f};
};

// Transposed direct form II. Keeping this in the header lets the RP2350 build
// inline the inner DSP loop instead of making millions of function calls.
inline float process_biquad(const BiquadCoeffs& c, BiquadState& s, float x) {
    // H(z) denominator is 1 + a1*z^-1 + a2*z^-2.
    const float y = c.b0 * x + s.z1;
    s.z1 = c.b1 * x - c.a1 * y + s.z2;
    s.z2 = c.b2 * x - c.a2 * y;
    return y;
}

}  // namespace dsp4
