# Pico 2: four stereo DSP outputs with five-minute receiver standby

Firmware for a Raspberry Pi Pico 2 or Pico 2 W, a PCM1808 stereo ADC,
four PCM5102A stereo DACs and an SPI W5500 Ethernet module. It accepts
one stereo 96 kHz input and produces four independently filtered stereo
outputs (eight output audio channels). A separate W5500 control connection
queries an Onkyo/eISCP-compatible receiver after five minutes of ADC
silence and requests standby only if the receiver reports power ON.

The supplied filter bank is a **generic bench example**, not a loudspeaker
design. Its crossover points and gains have not been calibrated for any
driver, amplifier or enclosure. Check routing and transfer functions with
test equipment before connecting loudspeakers. `approved_for_hardware` in
the JSON enables DSP output; it does not certify a speaker-specific setup.

## What runs where

| Device / core | Work |
| --- | --- |
| Pico 2 Core 0 | ADC DMA interrupts, W5500 polling and the pre-DSP silence timer |
| Pico 2 Core 1 | Stereo DSP and four stereo DAC DMA streams |
| PIO + DMA | Continuous I²S clocks and 512-frame double-buffered audio at 96 kHz |

The RP2350 runs at 144 MHz. The firmware does not resample and does not
provide USB audio, USB logging, Wi-Fi, a network player, automatic power-on,
input switching or volume control. Pico 2 W works as a board choice, but
its Wi-Fi radio is unused. USB BOOTSEL firmware flashing still works.

## Hardware and wiring

| Signal | Pico GPIO | Purpose |
| --- | ---: | --- |
| W5500 MISO, CS, SCK, MOSI, RESET, INT | 0, 1, 2, 3, 4, 5 | Control connection only |
| I²S LRCK and BCLK | 10, 12 | Shared ADC/DAC word and bit clocks |
| PCM1808 ADC data and 256× MCLK | 27, 28 | Stereo input and ADC master clock |
| PCM5102A DATA: LOW, LOW_MID, HIGH_MID, HIGH | 11, 13, 14, 15 | Four stereo outputs |

Share ground and the two I²S clock signals across the converter modules.
The PIO source waits on GPIO 10 and 12 directly; changing clock pins
requires updating both `src/main.cpp` and `src/audio_transport.pio`.
See [hardware photographs, capacitor modification and full wiring](docs/hardware_and_validation.md)
for the pictured boards and a staged scope and listening check. The ADC
module pictured there required removal of two specific SMD input capacitors
to achieve its measured flat response; test another module before making
that board-specific modification.

## Build

Install Python 3, a native C++ compiler, CMake, an ARM embedded compiler,
and a Pico SDK checkout with submodules. The GitHub workflow demonstrates
an Ubuntu 24.04 installation and uses SDK `2.2.0`. On macOS, install the
equivalent ARM toolchain and CMake, then set `PICO_SDK_PATH` to your checkout.

```sh
export PICO_SDK_PATH="/absolute/path/to/pico-sdk"
bash ./build.sh
```

The default builds the public demonstration bank for a plain Pico 2 and
writes `pico2_four_stereo_dsp_standby_pico2.uf2`. To use a Pico 2 W or your
own *local* filter file, set the environment variables before the command:

```sh
PICO_BOARD=pico2_w DSP_FILTER_JSON="/absolute/path/to/my_96k_coeffs.json" \
  bash ./build.sh
```

Quote paths with spaces. The private JSON, generated header and resulting
UF2 may all contain your filter settings. Keep your own JSON **outside
the repository** and share only the source/demo build. The build script
validates the selected bank, runs native DSP and standby checks, builds
the firmware, and prints the linker memory use. The generated header is
stored under `build_pico2/` or `build_pico2_w/`; both directories and the
UF2 output are gitignored. `bash host/run_native_tests.sh` runs without
the Pico SDK. The GitHub Actions workflow builds both board variants
using only the public demonstration bank; no speaker-specific firmware
is released here.

Hold BOOTSEL while connecting the board via USB. Copy the UF2 onto the
`RPI-RP2` volume. Flashing does not require the W5500 or Wi-Fi. There is
no serial diagnostics port in the running firmware.

## Configure the four bands

`config/example_filters_96k.json` contains an illustrative 96 kHz
four-band crossover with Linkwitz-Riley fourth-order pairs at **250 Hz,
1500 Hz and 6000 Hz**, with 12 dB of band attenuation and 6 dB of input
headroom. `host/make_example_filters.py` reproduces this JSON using only
the Python standard library. At each crossover the adjacent filter
responses are near −6 dB before band gain/headroom. No loudspeaker
measurements or private coefficients are included.

The firmware accepts `COEFF` entries only, each with `b0`, `b1`, `b2`,
`a1`, `a2`; two entries in series form each LR4 low-pass or high-pass.
Band IDs must be `LOW`, `LOW_MID`, `HIGH_MID`, `HIGH` in DAC pin order.
Each band also has `gain_db`, `polarity` and `delay_samples` settings.
Use the [standalone one-cell Jupyter notebook](notebooks/generic_filter_workflow_one_cell.ipynb)
to edit a frequency/Q/gain recipe, generate transfer-function graphs and
export 96 kHz `COEFF` JSON for the `DSP_FILTER_JSON` build variable. See
[filter design, supported types, conversion and coefficient convention](docs/filter_format.md)
for the full manual and the difference between JSON `block_size` metadata
and the fixed 512-frame DMA buffer. The notebook has no project Python
dependencies but requires NumPy, SciPy and Matplotlib in Jupyter.

## Five-minute standby

The ADC level is measured **before DSP** over every 512-frame stereo block.
If a block's combined L/R RMS reaches or exceeds 0.001 (−60 dBFS), it
restarts the timer. Silence begins at boot, even if audio has never played.
After 300 seconds below the threshold, Core 0 queries receiver power over
the W5500. A fresh ON reply triggers a standby request; an OFF reply does
not. Once the check completes the timer starts again for another 300
seconds. A lost Ethernet link or missing power reply causes retries after
a successful W5500 initialization, never a standby based on an old status.
If the W5500 is not detected at boot, receiver control stays disabled until
the next boot. New ADC audio cancels a pending check.

The firmware **monitors only its ADC**, regardless of the receiver's
selected input. If someone listens to another receiver input while this
ADC is silent, the receiver can still be switched off. It never turns
the receiver on. This behavior is deliberate for the automatic power-off
use case.

Edit `include/network_config.hpp` to set the W5500 IP, subnet, MAC and
receiver IP before connecting Ethernet. Its `192.168.1.*` addresses are
examples, and both devices must be reachable on the local network. The
firmware opens TCP port 60128 and sends Onkyo/eISCP power query and
standby commands only; an arbitrary amplifier will require a different
protocol implementation. No audio is carried on Ethernet.

## Verification status

The underlying audio transport and five-minute control behavior were
checked on a physical Pico 2 setup using a separate, privately calibrated
filter bank: clean audio on all four stereo outputs, and standby after
approximately five minutes of silence with two different receiver inputs.
This renamed, public example bank has native checks and a CI build recipe;
it **has not yet been checked on that hardware**. Scope measurements are
still needed to validate its example transfer functions and wiring. The
[test history](docs/test_history.md) identifies the ADC response, earlier
transport trials, eight-channel listening, two timed standby checks, and
remaining validation separately.

## License

MIT; see [LICENSE](LICENSE).
