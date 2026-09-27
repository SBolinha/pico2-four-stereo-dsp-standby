# Filter bank JSON and coefficient convention

The public example is `config/example_filters_96k.json`. Change this file
only to revise the public example. For a private design, put its JSON
outside the repository and pass its absolute path with `DSP_FILTER_JSON`.

| JSON field | Required value or meaning |
| --- | --- |
| `name` | Nonempty configuration name. `EXAMPLE`, `TEMPLATE` and `SYNTHETIC` are reserved for non-firmware fixtures. |
| `sample_rate` | Exactly `96000`; 44.1/48 kHz coefficients are rejected. |
| `block_size` | Positive metadata from the design program. The firmware DMA block stays **512 frames** regardless of this value. |
| `approved_for_hardware` | Must be the JSON boolean `true` to enable the output. This is an explicit enable switch, not speaker certification. |
| `input_headroom_db` | Pre-filter gain in dB (usually negative). |
| `max_delay_samples` | Largest allowed per-band delay; the live delay buffer is sized to the largest *configured* delay. |
| `bands` | Exactly four objects, ordered `LOW`, `LOW_MID`, `HIGH_MID`, `HIGH`. Each feeds its own stereo DAC. |
| `gain_db`, `polarity`, `delay_samples` | Gain in dB, `1` or `-1`, and integer sample delay for both L/R sides of that band. |
| `filters` | One or more `COEFF` objects, cascaded in order. Each contains finite numeric `b0`, `b1`, `b2`, `a1`, `a2`. |

Each biquad uses the transfer function

```text
                b0 + b1 z^-1 + b2 z^-2
H(z) = -----------------------------------------
                1  + a1 z^-1 + a2 z^-2
```

`a1` and `a2` are entered with the denominator sign convention shown;
the transposed direct-form-II implementation subtracts their feedback
terms. The generator rounds coefficients to float32 and rejects nonfinite
values or sections whose rounded poles are on or outside the unit circle.
Each band shares coefficient values between left/right but has separate
filter histories. A fourth-order section is two second-order `COEFF`
entries in series; the provided example has section counts 2 / 4 / 4 / 2.

The user-owned Python/Jupyter design program can export these fields;
there is no filter design computation on the Pico. If using another
design program, make sure its feedback-sign convention matches this one.
The bundled `host/make_example_filters.py` generates illustrative LR4
coefficient pairs at 96 kHz. To regenerate the public example:

```sh
python3 host/make_example_filters.py
bash host/run_native_tests.sh
```

The native tests compare block processing against the per-frame reference
with different chunk sizes, validate the public crossover amplitudes,
check four-band order and stability, and exercise the five-minute standby
state machine with a simulated receiver. These checks do not measure the
electrical DAC output.
