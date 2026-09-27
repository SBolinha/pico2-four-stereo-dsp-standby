#pragma once

#include <array>
#include <cstddef>

#include "biquad.hpp"
#include "dsp_types.hpp"
#include <generated_filters.hpp>

namespace dsp4 {

class DspEngine {
public:
    // Firmware uses the generated enable flag. It only enables DSP output;
    // it does not certify the filter bank for any specific loudspeaker.
    explicit DspEngine(bool enabled = generated::kApprovedForHardware);
    void reset();
    OutputFrame process(const StereoFrame& input);
    void process_block(const StereoFrame* input, OutputFrame* output,
                       std::size_t frame_count);

private:
    static constexpr std::size_t kScratchFrames = 512;
    static_assert(generated::kBands.size() == 4,
                  "The eight-channel DSP requires exactly four stereo bands");
    static_assert([]() constexpr {
        for (const auto& band : generated::kBands)
            if (band.section_count > generated::kMaxSections) return false;
        return true;
    }(), "Filter bank has more sections than DSP state slots");
    // The generator's kMaxDelaySamples may reserve 100 ms even when no band
    // uses any delay. Size the actual delay lines from the four band settings.
    static constexpr std::size_t kActiveDelaySamples = []() constexpr {
        std::size_t maximum = 0;
        for (const auto& band : generated::kBands)
            if (band.delay_samples > maximum) maximum = band.delay_samples;
        return maximum;
    }();
    struct DelayLine {
        std::array<float, kActiveDelaySamples + 1> data{};
        std::size_t write_index{0};
        float process(float x, std::size_t delay);
        void reset();
    };

    using States = std::array<BiquadState, generated::kMaxSections>;
    bool enabled_{false};
    std::array<States, 8> states_{};
    std::array<DelayLine, 8> delays_{};
    // Kept in the long-lived engine, not on Core 1's small stack. Reused for
    // every channel while processing each biquad over a contiguous block.
    std::array<StereoFrame, kScratchFrames> section_scratch_{};
};

}  // namespace dsp4
