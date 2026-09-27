#include <cmath>
#include <cstdio>
#include <vector>

#include "dsp_engine.hpp"

int main() {
    constexpr std::size_t count = 1553;  // More than three DMA blocks.
    std::vector<dsp4::StereoFrame> input(count);
    std::vector<dsp4::OutputFrame> sample_by_sample(count);
    std::vector<dsp4::OutputFrame> processed_in_blocks(count);
    for (std::size_t i = 0; i < count; ++i)
        input[i] = {0.15f * std::sin(0.039f * float(i)),
                    0.11f * std::cos(0.017f * float(i))};

    dsp4::DspEngine reference;
    dsp4::DspEngine optimized;
    for (int run = 0; run < 2; ++run) {
        if (run) { reference.reset(); optimized.reset(); }
        for (std::size_t i = 0; i < count; ++i)
            sample_by_sample[i] = reference.process(input[i]);
        if (run == 0) {
            optimized.process_block(input.data(), processed_in_blocks.data(), count);
        } else {
            const std::size_t chunks[] = {1, 17, 494, 512, 2, 527};
            std::size_t offset = 0;
            for (auto size : chunks) {
                if (offset == count) break;
                if (size > count - offset) size = count - offset;
                optimized.process_block(input.data() + offset,
                                        processed_in_blocks.data() + offset,
                                        size);
                offset += size;
            }
            if (offset < count)
                optimized.process_block(input.data() + offset,
                                        processed_in_blocks.data() + offset,
                                        count - offset);
        }
        bool nonzero = false;
        for (std::size_t i = 0; i < count; ++i) {
            for (std::size_t ch = 0; ch < 8; ++ch) {
                const float a = sample_by_sample[i][ch];
                const float b = processed_in_blocks[i][ch];
                if (!std::isfinite(b) || std::fabs(a - b) > 1e-6f) {
                    std::fprintf(stderr, "DSP mismatch frame %zu channel %zu\n", i, ch);
                    return 1;
                }
                nonzero |= b != 0.0f;
            }
        }
        if (!nonzero) return 2;
    }
    std::puts("PASS: 96 kHz generated bank; eight channels, delay, polarity, "
              "stereo sections and arbitrary block boundaries");
    return 0;
}
