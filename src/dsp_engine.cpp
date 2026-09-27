#include "dsp_engine.hpp"

#include <algorithm>

namespace dsp4 {

DspEngine::DspEngine(bool enabled) : enabled_(enabled) { reset(); }

void DspEngine::DelayLine::reset() {
    data.fill(0.0f);
    write_index = 0;
}

float DspEngine::DelayLine::process(float x, std::size_t delay) {
    if (delay == 0) return x;
    delay = std::min(delay, kActiveDelaySamples);
    data[write_index] = x;
    const std::size_t n = data.size();
    const std::size_t read_index = write_index >= delay
        ? write_index - delay : write_index + (n - delay);
    const float y = data[read_index];
    if (++write_index == n) write_index = 0;
    return y;
}

void DspEngine::reset() {
    for (auto& channel : states_) {
        for (auto& state : channel) state = {};
    }
    for (auto& delay : delays_) delay.reset();
}

// The per-frame reference remains useful for verifying block processing and
// for callers that need exactly one input frame at a time.
OutputFrame DspEngine::process(const StereoFrame& input) {
    OutputFrame out{};
    if (!enabled_) return out;
    const float in[2] = {
        input.left * generated::kInputHeadroom,
        input.right * generated::kInputHeadroom
    };
    for (std::size_t band = 0; band < generated::kBands.size(); ++band) {
        const auto& definition = generated::kBands[band];
        for (std::size_t side = 0; side < 2; ++side) {
            const std::size_t channel = 2 * band + side;
            float sample = in[side];
            for (std::size_t section = 0; section < definition.section_count;
                 ++section) {
                sample = process_biquad(definition.sections[section],
                                         states_[channel][section], sample);
            }
            sample *= definition.signed_gain;
            out[channel] = delays_[channel].process(sample,
                                                    definition.delay_samples);
        }
    }
    return out;
}

void DspEngine::process_block(const StereoFrame* input, OutputFrame* output,
                              std::size_t frame_count) {
    if (!enabled_) {
        std::fill_n(output, frame_count, OutputFrame{});
        return;
    }
    while (frame_count != 0) {
        const std::size_t count =
            std::min(frame_count, section_scratch_.size());
        for (std::size_t band = 0; band < generated::kBands.size(); ++band) {
            const BandDefinition& definition = generated::kBands[band];
            // Each band has one coefficient bank shared by both sides, with
            // separate L/R filter states. Reuse the five coefficient values
            // over the whole block and both channels before loading another
            // section. The order of operations within each channel is the
            // same as in process().
            for (std::size_t section = 0;
                 section < definition.section_count; ++section) {
                const BiquadCoeffs& c = definition.sections[section];
                BiquadState& left = states_[2 * band][section];
                BiquadState& right = states_[2 * band + 1][section];
                float lz1 = left.z1, lz2 = left.z2;
                float rz1 = right.z1, rz2 = right.z2;
                const float b0 = c.b0, b1 = c.b1, b2 = c.b2;
                const float a1 = c.a1, a2 = c.a2;
                if (section == 0) {
                    for (std::size_t i = 0; i < count; ++i) {
                        const float xl = input[i].left *
                                         generated::kInputHeadroom;
                        const float xr = input[i].right *
                                         generated::kInputHeadroom;
                        const float yl = b0 * xl + lz1;
                        const float yr = b0 * xr + rz1;
                        lz1 = b1 * xl - a1 * yl + lz2;
                        rz1 = b1 * xr - a1 * yr + rz2;
                        lz2 = b2 * xl - a2 * yl;
                        rz2 = b2 * xr - a2 * yr;
                        section_scratch_[i] = {yl, yr};
                    }
                } else {
                    for (std::size_t i = 0; i < count; ++i) {
                        const float xl = section_scratch_[i].left;
                        const float xr = section_scratch_[i].right;
                        const float yl = b0 * xl + lz1;
                        const float yr = b0 * xr + rz1;
                        lz1 = b1 * xl - a1 * yl + lz2;
                        rz1 = b1 * xr - a1 * yr + rz2;
                        lz2 = b2 * xl - a2 * yl;
                        rz2 = b2 * xr - a2 * yr;
                        section_scratch_[i] = {yl, yr};
                    }
                }
                left.z1 = lz1; left.z2 = lz2;
                right.z1 = rz1; right.z2 = rz2;
            }

            if (definition.delay_samples == 0) {
                for (std::size_t i = 0; i < count; ++i) {
                    const float xl = definition.section_count != 0 ?
                        section_scratch_[i].left :
                        input[i].left * generated::kInputHeadroom;
                    const float xr = definition.section_count != 0 ?
                        section_scratch_[i].right :
                        input[i].right * generated::kInputHeadroom;
                    output[i][2 * band] = xl * definition.signed_gain;
                    output[i][2 * band + 1] = xr * definition.signed_gain;
                }
            } else {
                for (std::size_t i = 0; i < count; ++i) {
                    const float xl = definition.section_count != 0 ?
                        section_scratch_[i].left :
                        input[i].left * generated::kInputHeadroom;
                    const float xr = definition.section_count != 0 ?
                        section_scratch_[i].right :
                        input[i].right * generated::kInputHeadroom;
                    output[i][2 * band] = delays_[2 * band].process(
                        xl * definition.signed_gain,
                        definition.delay_samples);
                    output[i][2 * band + 1] =
                        delays_[2 * band + 1].process(
                            xr * definition.signed_gain,
                            definition.delay_samples);
                }
            }
        }
        input += count;
        output += count;
        frame_count -= count;
    }
}

}  // namespace dsp4
