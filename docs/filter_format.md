# From filter parameters to a 96 kHz firmware build

There are **two JSON stages**. In the one-cell notebook you edit a compact
*parameter recipe* with filter types, cutoff frequencies, Q values and
gain. Run the cell once to draw the transfer functions and produce a
*compile-ready coefficient bank*. Save that resulting JSON file locally;
`build.sh` converts it to a C++ header for the Pico 2. The Pico performs
no coefficient calculation at boot. A private bank can stay outside the
public repository throughout this process.

## One-cell Jupyter workflow

1. Open [`notebooks/generic_filter_workflow_one_cell.ipynb`](../notebooks/generic_filter_workflow_one_cell.ipynb)
   in JupyterLab or Jupyter Notebook. In the notebook's Python environment,
   install `numpy`, `scipy` and `matplotlib`, for example with
   `python3 -m pip install numpy scipy matplotlib`. Its **only code cell**
   contains the entire calculation, validation, plots and JSON export;
   it does not import Python modules from this repository.
2. Edit `SOURCE_CONFIG_JSON` at the top of that cell. The shipped recipe
   describes an **illustrative, uncalibrated** four-band LR4 crossover at
   250/1500/6000 Hz. Preserve the required band IDs and their order:
   `LOW`, `LOW_MID`, `HIGH_MID`, `HIGH`. Add filter objects in the desired
   signal-flow order. See the filter table below.
3. Run the cell. It designs biquads at `TARGET_SAMPLE_RATE = 96000`, checks
   float32 stability, plots the individual stages, cumulative magnitude,
   four output magnitudes and phases, and prints a headroom estimate.
   Plotted transfer functions are electrical calculations only; they do
   not include ADC/DAC analog behavior, speaker acoustics or the room.
4. Copy the complete JSON **between** `BEGIN COMPILE-READY JSON` and
   `END COMPILE-READY JSON` into a new local file, e.g.
   `/Users/you/PrivateFilters/my_96k_coeffs.json`. Copy from the opening
   `{` through the matching closing `}`; do not copy marker lines or
   surrounding output. Alternatively, set `OUTPUT_JSON_PATH` at the top
   of the same cell to that absolute file path and run the cell again.
5. From the checkout containing `build.sh`, build a plain Pico 2:

   ```sh
   export PICO_SDK_PATH="/absolute/path/to/pico-sdk"
   DSP_FILTER_JSON="/Users/you/PrivateFilters/my_96k_coeffs.json" \
     PICO_BOARD=pico2 bash ./build.sh
   ```

   `DSP_FILTER_JSON` is the **file path**, not the JSON text. Keep spaces
   inside normal shell quotes: `DSP_FILTER_JSON="/Users/you/Speaker GPT/my.json"`.
   Avoid a backslash before the space **inside** those quotes. For a Pico
   2 W replace `pico2` with `pico2_w`; its Wi-Fi stays unused. The UF2 is
   written at the checkout root; the generated C++ header and temporary
   builds stay in ignored `build_pico2/` or `build_pico2_w/`. Neither the
   local JSON nor a private UF2 should be added to a public commit.

For a quick public demonstration build, `bash ./build.sh` defaults to
`config/example_filters_96k.json`. That file is also reproduced, without
SciPy, by `python3 host/make_example_filters.py`; the notebook starts with
the equivalent public parameter recipe. The example's headroom and band
attenuation are for a bench demonstration and do not configure real drivers.

## Parameter recipe fields

```json
{
  "name": "MY_FOUR_BAND_BANK",
  "sample_rate": 96000,
  "block_size": 512,
  "approved_for_hardware": true,
  "input_headroom_db": -6.0,
  "max_delay_samples": 2048,
  "bands": [
    {"id": "LOW", "gain_db": -12, "polarity": 1,
     "delay_samples": 0,
     "filters": [{"type": "LR4_LP", "freq": 250}]},
    {"id": "LOW_MID", "gain_db": -12, "polarity": 1,
     "delay_samples": 0,
     "filters": [{"type": "LR4_HP", "freq": 250},
                 {"type": "LR4_LP", "freq": 1500}]},
    {"id": "HIGH_MID", "gain_db": -12, "polarity": 1,
     "delay_samples": 0,
     "filters": [{"type": "LR4_HP", "freq": 1500},
                 {"type": "LR4_LP", "freq": 6000}]},
    {"id": "HIGH", "gain_db": -12, "polarity": 1,
     "delay_samples": 0,
     "filters": [{"type": "LR4_HP", "freq": 6000}]}
  ]
}
```

This shortened example is an *input recipe*: `LR4_LP` and `LR4_HP` are
expanded by the cell into two `COEFF` objects each. The full public
example is already in the notebook. Filter frequencies must be above zero
and below the **target** Nyquist frequency, 48 kHz at 96 kHz sampling.
Q must be positive; gains and coefficients must be finite.

| Field | Meaning |
| --- | --- |
| `name` | Nonempty bank name. Names containing `EXAMPLE`, `TEMPLATE` or `SYNTHETIC` are rejected by the normal firmware compiler. |
| `sample_rate` | Source recipe rate, used to convert *time* delays. The notebook designs the output biquads afresh at `TARGET_SAMPLE_RATE = 96000`. The **compiled** JSON must say `96000`. |
| `block_size` | Positive recipe metadata. The current firmware uses **512 stereo frames per DMA block**; the notebook's `TARGET_BLOCK_SIZE = 512` writes that value to its output. Editing the JSON alone does not resize DMA. |
| `approved_for_hardware` | Must be JSON `true` for an enabled firmware build. This is a manual output enable flag, **not** a certificate for any amplifier/speaker. |
| `input_headroom_db` | Gain applied once to the stereo ADC signal before the four filters; a negative value reduces the risk of clipping. |
| `max_delay_samples` / `max_delay_ms` | One limit in the input recipe, expressed in samples at the *source* rate or milliseconds. The cell writes target-rate `max_delay_samples`. Only the sample form is allowed in the compiled bank. |
| `bands` | Exactly four entries in physical DAC order `LOW`, `LOW_MID`, `HIGH_MID`, `HIGH`. `label` is optional plot text. |
| `gain_db` / `polarity` | Per-band gain in dB and `1` or `-1`; the same settings apply to L/R. |
| `delay_samples` / `delay_ms` | Per-band delay, at the source rate or in milliseconds; use the same unit choice as the maximum. At 96 kHz, 1 ms = 96 samples. Delay memory is allocated for the longest **configured** band delay. |
| `filters` | Ordered array of filter objects; all stages are cascaded. Each band must generate at least one section. |

If adapting a 44.1 kHz parameter recipe, change the *target* to 96 kHz
while keeping its cutoff/centre frequencies, Qs and gains as **design
parameters**, then inspect the new graphs and physical measurement.
Sample delays are converted through milliseconds so the time alignment is
maintained. Raw 44.1/48 kHz biquad coefficients **must be redesigned**;
the notebook rejects raw `COEFF`, `BIQUAD` or `FREE` objects whenever the
source and target sample rates differ. It cannot reconstruct a filter's
original intent from arbitrary five-number coefficients.

## Available filter types in the notebook

Every filter object has a `type`. Types are case-insensitive. `freq` is
in hertz; `q` defaults to 1/√2 for filters that use it; `gain_db` defaults
to 0 dB. The aliases in a row produce the same shape. The numeral `2`
or `4` is the nominal filter order / number of second-order sections;
fourth-order designs are represented as a **cascade of two biquads**, not
as one direct fourth-order recurrence.

| Canonical type | Accepted aliases | Values | Biquads / description |
| --- | --- | --- | --- |
| `LP2`, `HP2` | — | `freq`; optional `q` | 1, configurable-Q low/high-pass. |
| `LR2_LP`, `LR2_HP` | `LP2_LR`, `HP2_LR` | `freq` | 1, Linkwitz–Riley 2nd order (Q = 0.5). |
| `LR4_LP`, `LR4_HP` | `LP4_LR`, `HP4_LR` | `freq` | 2 identical Butterworth biquads (Q = 1/√2), Linkwitz–Riley 4th order. |
| `BW2_LP`, `BW2_HP` | `LP2_BW`, `HP2_BW` | `freq` | 1, Butterworth 2nd order (Q = 1/√2). |
| `BW4_LP`, `BW4_HP` | `LP4_BW`, `HP4_BW` | `freq` | 2 Butterworth 4th-order sections (Q ≈ 0.541196 and 1.306563). |
| `PEQ2` | `PEAK2`, `PK2` | `freq`; optional `q`, `gain_db` | 1, peaking equalizer; gain specified at the peak centre. |
| `PEQ4` | `PEAK4`, `PK4` | `freq`; optional `q`, `gain_db` | 2 identical peaking biquads, each with **half** the specified gain in dB. |
| `LOWSHELF2`, `HIGHSHELF2` | `LS2`, `HS2` | `freq`; optional `q`, `gain_db` | 1 RBJ-style low/high shelf; this implementation uses α = sin(2πf/fs)/(2Q), not an `S` field. |
| `LOWSHELF4`, `HIGHSHELF4` | `LS4`, `HS4` | `freq`; optional `q`, `gain_db` | 2 shelves of the same orientation, each with **half** the specified gain in dB. |
| `COEFF` | `BIQUAD`, `FREE` | `b0`, `b1`, `b2`, `a1`, `a2` | 1 raw biquad at **the source rate only**; the notebook emits `COEFF` in its output. |

For `LR2`, `LR4`, `BW2` and `BW4`, supplied `q` is ignored because those
families set Q internally. For nonzero peak/shelf gains specify `gain_db`;
`PEQ4` and four-pole shelves divide this dB gain over the two sections.
The notebook validates float32 pole stability for each generated section;
this does not measure full-system clipping, acoustic phase or output level.

## Compile-ready coefficient JSON

The notebook output preserves the bank and band fields and replaces every
recipe stage by one or two objects of this form:

```json
{"type": "COEFF", "b0": 0.5, "b1": 0, "b2": 0,
 "a1": 0, "a2": 0}
```

This is an **illustrative single biquad**, not a useful crossover. The
denominator uses the following sign convention:

```text
                b0 + b1 z^-1 + b2 z^-2
H(z) = -------------------------------------
                1  + a1 z^-1 + a2 z^-2
```

The firmware subtracts `a1` and `a2` feedback terms internally. If your
design software stores feedback with the opposite sign, invert those
numbers before creating its `COEFF` entries. The Python compiler rounds
all five coefficients to float32, rejects non-finite numbers or unstable
rounded poles, then writes the C++ header. A band's left and right sides
reuse its coefficient bank but have **separate filter histories**.
One `LR4` stage has two `COEFF` objects, one for each biquad. The public
bank has **2 / 4 / 4 / 2** sections in DAC order.

Run the host-only checks without a Pico SDK:

```sh
bash host/run_native_tests.sh
```

They check the public bank's schema/stability, block processing versus a
sample reference, a few crossover amplitudes, and simulated five-minute
receiver state transitions. They cannot replace analog measurements on
the final four-DAC hardware; see [validation record](test_history.md).
