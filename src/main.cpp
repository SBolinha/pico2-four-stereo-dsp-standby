#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "dsp_engine.hpp"
#include "generated_filters.hpp"
#include "onkyo_five_minute_off.hpp"
#include "sample_convert.hpp"
#include "w5500_onkyo.h"
#include "audio_transport.pio.h"

namespace {

// The two PIO clocks, ADC, four DACs, and DSP share this *native* sample rate.
constexpr std::uint32_t kSampleRate = 96000u;
constexpr std::size_t kBlockFrames = 512;
constexpr std::size_t kWordsPerBlock = kBlockFrames * 2;
constexpr std::size_t kDacCount = 4;
constexpr std::size_t kBuffers = 2;

static_assert(dsp4::generated::kSampleRate == kSampleRate,
              "Generate 96 kHz coefficients from a 96 kHz COEFF JSON");
static_assert(dsp4::generated::kBlockSize == kBlockFrames,
              "Generator and DMA must agree on 512-frame blocks");
static_assert(dsp4::generated::kApprovedForHardware,
              "The selected configuration must explicitly enable DSP output");
static_assert(dsp4::generated::kBands.size() == kDacCount,
              "Expected four stereo output bands");

// Pin assignments for the four stereo outputs.
constexpr uint kLrckPin = 10, kBclkPin = 12, kAdcPin = 27, kMclkPin = 28;
constexpr uint kDacPins[kDacCount] = {11, 13, 14, 15};
constexpr uint kClockSm = 2, kMclkSm = 0, kAdcSm = 1;
constexpr uint kDacSms[kDacCount] = {0, 1, 2, 3};
PIO const kClockPio = pio1;
PIO const kDacPio = pio0;

// This is a pre-DSP, per-block RMS threshold: 0.001 is -60 dBFS.
constexpr float kSignalThreshold = 0.001f;

using RawBlock = std::array<std::uint32_t, kWordsPerBlock>;
alignas(4) std::array<RawBlock, kBuffers> g_adc_blocks{};
alignas(4) std::array<std::array<RawBlock, kDacCount>, kBuffers> g_dac_blocks{};
std::array<dsp4::StereoFrame, kBlockFrames> g_input{};
std::array<dsp4::OutputFrame, kBlockFrames> g_output{};
dsp4::DspEngine g_dsp;

int g_adc_dma[kBuffers] = {-1, -1};
int g_dac_dma[kBuffers][kDacCount]{};
std::uint32_t g_dac_done[kBuffers]{};
std::uint32_t g_adc_irq_channels = 0;
std::uint32_t g_dac_irq_channels = 0;

// The ADC IRQ runs only on Core 0; the DAC IRQ and filter path run on Core 1.
volatile std::uint32_t g_adc_ready = 0;
volatile std::uint32_t g_dac_free = 0;
volatile std::uint32_t g_core1_ready = 0;
// Core 1 updates this for every ADC block above the threshold. Core 0
// treats its initial value (set just before start_audio) as boot silence.
volatile std::uint32_t g_last_above_threshold_ms = 0;

void adc_irq() {
    const std::uint32_t pending = dma_hw->ints0 & g_adc_irq_channels;
    dma_hw->ints0 = pending;
    for (std::size_t b = 0; b < kBuffers; ++b) {
        const int channel = g_adc_dma[b];
        if (!(pending & (1u << channel))) continue;
        dma_channel_set_write_addr(channel, g_adc_blocks[b].data(), false);
        dma_channel_set_trans_count(channel, kWordsPerBlock, false);
        __atomic_fetch_or(&g_adc_ready, 1u << b, __ATOMIC_RELEASE);
    }
}

void dac_irq() {
    const std::uint32_t pending = dma_hw->ints1 & g_dac_irq_channels;
    dma_hw->ints1 = pending;
    for (std::size_t b = 0; b < kBuffers; ++b) {
        for (std::size_t lane = 0; lane < kDacCount; ++lane) {
            const int channel = g_dac_dma[b][lane];
            if (!(pending & (1u << channel))) continue;
            dma_channel_set_read_addr(
                channel, g_dac_blocks[b][lane].data(), false);
            dma_channel_set_trans_count(channel, kWordsPerBlock, false);
            g_dac_done[b] |= 1u << lane;
        }
        if (g_dac_done[b] != ((1u << kDacCount) - 1u)) continue;
        g_dac_done[b] = 0;
        __atomic_fetch_or(&g_dac_free, 1u << b, __ATOMIC_RELEASE);
    }
}

struct ClockDividers { float i2s; float mclk; };
ClockDividers clock_dividers() {
    const std::uint32_t sys = clock_get_hz(clk_sys);
    std::uint32_t fixed = (sys + kSampleRate / 2u) / kSampleRate;
    if (fixed & 1u) ++fixed;
    return {static_cast<float>(fixed) / 256.0f,
            static_cast<float>(fixed / 2u) / 256.0f};
}

void configure_pio() {
    pio_sm_claim(kClockPio, kClockSm);
    pio_sm_claim(kClockPio, kMclkSm);
    pio_sm_claim(kClockPio, kAdcSm);
    for (uint sm : kDacSms) pio_sm_claim(kDacPio, sm);

    const uint clock_off = pio_add_program(kClockPio, &audio_i2s_clock_32_program);
    const uint mclk_off = pio_add_program(kClockPio, &pcm1808_mclk_256_program);
    const uint adc_off = pio_add_program(kClockPio, &pcm1808_i2s_rx_32_program);
    const uint dac_off = pio_add_program(kDacPio, &pcm5102_i2s_tx_32_program);
    for (uint pin : {kLrckPin, kBclkPin, kMclkPin, kAdcPin})
        pio_gpio_init(kClockPio, pin);
    for (uint pin : kDacPins) pio_gpio_init(kDacPio, pin);

    const auto div = clock_dividers();
    pio_sm_config clock_cfg = audio_i2s_clock_32_program_get_default_config(clock_off);
    sm_config_set_set_pins(&clock_cfg, kLrckPin, 1);
    sm_config_set_sideset_pins(&clock_cfg, kBclkPin);
    sm_config_set_clkdiv(&clock_cfg, div.i2s);
    pio_sm_init(kClockPio, kClockSm,
                clock_off + audio_i2s_clock_32_offset_entry_point, &clock_cfg);
    pio_sm_set_consecutive_pindirs(kClockPio, kClockSm, kLrckPin, 1, true);
    pio_sm_set_consecutive_pindirs(kClockPio, kClockSm, kBclkPin, 1, true);
    pio_sm_set_pins_with_mask(kClockPio, kClockSm, 0,
                              (1u << kLrckPin) | (1u << kBclkPin));

    pio_sm_config mclk_cfg = pcm1808_mclk_256_program_get_default_config(mclk_off);
    sm_config_set_sideset_pins(&mclk_cfg, kMclkPin);
    sm_config_set_clkdiv(&mclk_cfg, div.mclk);
    pio_sm_init(kClockPio, kMclkSm, mclk_off, &mclk_cfg);
    pio_sm_set_consecutive_pindirs(kClockPio, kMclkSm, kMclkPin, 1, true);
    pio_sm_set_pins_with_mask(kClockPio, kMclkSm, 0, 1u << kMclkPin);
    gpio_set_slew_rate(kMclkPin, GPIO_SLEW_RATE_FAST);
    gpio_set_drive_strength(kMclkPin, GPIO_DRIVE_STRENGTH_4MA);

    pio_sm_config adc_cfg = pcm1808_i2s_rx_32_program_get_default_config(adc_off);
    sm_config_set_in_pins(&adc_cfg, kAdcPin);
    sm_config_set_in_shift(&adc_cfg, false, true, 32);
    sm_config_set_fifo_join(&adc_cfg, PIO_FIFO_JOIN_RX);
    pio_sm_init(kClockPio, kAdcSm,
                adc_off + pcm1808_i2s_rx_32_offset_entry_point, &adc_cfg);
    pio_sm_set_consecutive_pindirs(kClockPio, kAdcSm, kAdcPin, 1, false);

    for (std::size_t lane = 0; lane < kDacCount; ++lane) {
        pio_sm_config cfg = pcm5102_i2s_tx_32_program_get_default_config(dac_off);
        sm_config_set_out_pins(&cfg, kDacPins[lane], 1);
        sm_config_set_out_shift(&cfg, false, true, 32);
        sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_TX);
        pio_sm_init(kDacPio, kDacSms[lane],
                    dac_off + pcm5102_i2s_tx_32_offset_entry_point, &cfg);
        pio_sm_set_consecutive_pindirs(
            kDacPio, kDacSms[lane], kDacPins[lane], 1, true);
        pio_sm_set_pins_with_mask(kDacPio, kDacSms[lane], 0,
                                  1u << kDacPins[lane]);
    }
}

void configure_dma() {
    for (std::size_t b = 0; b < kBuffers; ++b) {
        g_adc_dma[b] = dma_claim_unused_channel(true);
        g_adc_irq_channels |= 1u << g_adc_dma[b];
        for (std::size_t lane = 0; lane < kDacCount; ++lane) {
            g_dac_dma[b][lane] = dma_claim_unused_channel(true);
            g_dac_irq_channels |= 1u << g_dac_dma[b][lane];
        }
    }
    for (std::size_t b = 0; b < kBuffers; ++b) {
        const int input_channel = g_adc_dma[b];
        dma_channel_config input = dma_channel_get_default_config(input_channel);
        channel_config_set_transfer_data_size(&input, DMA_SIZE_32);
        channel_config_set_read_increment(&input, false);
        channel_config_set_write_increment(&input, true);
        channel_config_set_dreq(&input, pio_get_dreq(kClockPio, kAdcSm, false));
        channel_config_set_chain_to(&input, g_adc_dma[1u - b]);
        dma_channel_configure(input_channel, &input, g_adc_blocks[b].data(),
                              &kClockPio->rxf[kAdcSm], kWordsPerBlock, false);
        dma_channel_set_irq0_enabled(input_channel, true);
        for (std::size_t lane = 0; lane < kDacCount; ++lane) {
            const int channel = g_dac_dma[b][lane];
            dma_channel_config output = dma_channel_get_default_config(channel);
            channel_config_set_transfer_data_size(&output, DMA_SIZE_32);
            channel_config_set_read_increment(&output, true);
            channel_config_set_write_increment(&output, false);
            channel_config_set_dreq(
                &output, pio_get_dreq(kDacPio, kDacSms[lane], true));
            channel_config_set_chain_to(&output, g_dac_dma[1u - b][lane]);
            dma_channel_configure(channel, &output, &kDacPio->txf[kDacSms[lane]],
                                  g_dac_blocks[b][lane].data(), kWordsPerBlock, false);
            dma_channel_set_irq1_enabled(channel, true);
        }
    }
    irq_set_exclusive_handler(DMA_IRQ_0, adc_irq);
    irq_set_enabled(DMA_IRQ_0, true);
}

void start_audio() {
    std::uint32_t channels = 1u << g_adc_dma[0];
    for (std::size_t lane = 0; lane < kDacCount; ++lane)
        channels |= 1u << g_dac_dma[0][lane];
    dma_start_channel_mask(channels);
    pio_sm_set_enabled(kClockPio, kMclkSm, true);
    sleep_us(10);
    for (uint sm : kDacSms) pio_sm_set_enabled(kDacPio, sm, true);
    pio_sm_set_enabled(kClockPio, kAdcSm, true);
    pio_sm_set_enabled(kClockPio, kClockSm, true);
}

void process_block(std::size_t b) {
    if (dma_channel_is_busy(g_adc_dma[b])) return;
    for (std::size_t lane = 0; lane < kDacCount; ++lane) {
        if (dma_channel_is_busy(g_dac_dma[b][lane])) return;
    }

    float energy = 0.0f;
    for (std::size_t i = 0; i < kBlockFrames; ++i) {
        const float left = dsp4::pcm_s24_left_justified_to_float(
            static_cast<std::int32_t>(g_adc_blocks[b][2*i]));
        const float right = dsp4::pcm_s24_left_justified_to_float(
            static_cast<std::int32_t>(g_adc_blocks[b][2*i + 1]));
        g_input[i] = {left, right};
        energy += left*left + right*right;
    }

    // DSP runs on every ADC frame, including silence. Neither input gating
    // nor an Onkyo status check alters the audio samples sent to the DACs.
    g_dsp.process_block(g_input.data(), g_output.data(), kBlockFrames);
    for (std::size_t i = 0; i < kBlockFrames; ++i) {
        for (std::size_t lane = 0; lane < kDacCount; ++lane) {
            g_dac_blocks[b][lane][2*i] = static_cast<std::uint32_t>(
                dsp4::float_to_pcm_s32(g_output[i][2*lane]));
            g_dac_blocks[b][lane][2*i + 1] = static_cast<std::uint32_t>(
                dsp4::float_to_pcm_s32(g_output[i][2*lane + 1]));
        }
    }

    const float threshold_energy = kSignalThreshold * kSignalThreshold *
                                   static_cast<float>(2u * kBlockFrames);
    if (energy >= threshold_energy)
        __atomic_store_n(&g_last_above_threshold_ms,
                         to_ms_since_boot(get_absolute_time()), __ATOMIC_RELEASE);
}

void audio_core() {
    irq_set_exclusive_handler(DMA_IRQ_1, dac_irq);
    irq_set_enabled(DMA_IRQ_1, true);
    __atomic_store_n(&g_core1_ready, 1u, __ATOMIC_RELEASE);
    while (true) {
        const std::uint32_t ready =
            __atomic_load_n(&g_adc_ready, __ATOMIC_ACQUIRE) &
            __atomic_load_n(&g_dac_free, __ATOMIC_ACQUIRE);
        if (!ready) { tight_loop_contents(); continue; }
        const std::uint32_t bit = ready & (~ready + 1u);
        __atomic_fetch_and(&g_adc_ready, ~bit, __ATOMIC_ACQ_REL);
        __atomic_fetch_and(&g_dac_free, ~bit, __ATOMIC_ACQ_REL);
        process_block((bit & 1u) ? 0u : 1u);
    }
}

// Only Core 0 issues receiver commands. No input selection, volume change
// or automatic power-on is performed by this firmware.
OnkyoFiveMinuteOff g_onkyo_five_minute_off;

void service_onkyo() {
    // Read the timestamp before the clock: a newly published Core 1
    // timestamp must not appear to be in the future due to a read race.
    const std::uint32_t last_above =
        __atomic_load_n(&g_last_above_threshold_ms, __ATOMIC_ACQUIRE);
    const std::uint32_t now = to_ms_since_boot(get_absolute_time());
    g_onkyo_five_minute_off.service(now, last_above);
}

}  // namespace

int main() {
    set_sys_clock_hz(144000000u, true);
    configure_pio();
    configure_dma();
    const bool w5500_ok = w5500_onkyo_init();
    __atomic_store_n(&g_last_above_threshold_ms,
                     to_ms_since_boot(get_absolute_time()), __ATOMIC_RELEASE);
    multicore_launch_core1(audio_core);
    while (__atomic_load_n(&g_core1_ready, __ATOMIC_ACQUIRE) == 0u)
        tight_loop_contents();
    start_audio();

    std::uint32_t next_poll = 0;
    while (true) {
        const std::uint32_t now = to_ms_since_boot(get_absolute_time());
        if (static_cast<std::int32_t>(now - next_poll) >= 0) {
            next_poll = now + 100;  // 10 Hz control polling; no audio over Ethernet.
            if (w5500_ok) service_onkyo();
        }
        sleep_us(500);
    }
}
