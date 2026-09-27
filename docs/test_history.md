# Validation record and limits

There were several firmware stages during development. The audible and
receiver tests below used a **locally tuned, privately held coefficient
bank** on actual hardware. The published repository carries a **generic
example bank**; it has not inherited hardware approval for a particular
loudspeaker. No private transfer curve, speaker parameter or coefficient
table is part of this repository.

| Test | What was done | Observed result | Applies to |
| --- | --- | --- | --- |
| PCM1808 analog input response | Captured stereo tones / sweep through the ADC module before and after removing the two circled input capacitors; also checked left/right agreement. | Before: pronounced high-frequency loss, roughly 218 Hz corner and about −25.6 dB at 20 kHz. After: approximately −0.21 dB at 20 kHz and approximately 0.03 dB L/R difference there; 20 Hz–20 kHz essentially flat for that board. | The pictured PCM1808 breakout and measurement chain; independent of the published crossover. |
| Earlier ADC loopback / USB capture | Ran the 44.1 kHz converter loopback and PC USB recording while investigating the analog input. | PCM1808 capture and DAC loopback worked; the corrected sweep showed the flat modified ADC input response. | Earlier diagnostic firmware, **not** a claim that USB audio is in the present build. |
| ADC, LMS and USB input trials | Recorded silence/music/silence sequences for each transport in an earlier combined-source firmware. | Workload and audio behavior were compared; this motivated the ADC-only 96 kHz design. | Earlier, more complex firmware. LMS and USB playback were removed from this release. |
| Native 96 kHz, four stereo DACs | Flashed an ADC-only, audio-focused predecessor with privately tuned coefficients and listened with four stereo DACs / four receiver channels. | All four stereo outputs reproduced useful, clean audio; the subsequent audio-only build was reported free of audible digital artifacts, including during silence with DSP still running. | That predecessor hardware and its private bank. No measured worst-case core timing or long-duration error count is claimed. |
| Receiver standby at silence from boot | Booted Pico at 15:27 with CD input selected and no ADC music. | Receiver went to standby at 15:32, about five minutes after boot. | Predecessor hardware build with W5500 and Onkyo-compatible receiver. |
| Receiver standby on a different selected input | Turned receiver on again with GAME selected, still no ADC music. | Receiver entered standby again at 15:37. | Same predecessor; receiver input selection did not inhibit the five-minute timer. |
| Four-band DSP calculations | `host/run_native_tests.sh` generates public coefficients, compiles the native DSP and standby tests, and checks state progression, channel mapping, filter stability and representative crossover response. | Run this on a checkout to reproduce; the included automated tests exercise **software** behavior only. | Published source/example bank. |
| Published generic UF2 on the exact pictured rig | Rigol tone/sweep of each final analog DAC output, detailed frequency, phase and gain verification, plus extended timing/load test. | **Pending.** No lab result for the public demonstration bank is asserted here. | Published source with public bank. |

The initial scope RMS sweep of the ADC appeared to show a roll-off that
depended on acquisition timebase. The later captured-audio sweep was used
to establish the response after the capacitor change; scope setup should
be checked when repeating measurements. Subjective listening validates
that the tuned predecessor worked for that setup; it does not measure
the public crossover, latency, thermal margin or analog frequency response.

## Repeatable checks for a new configuration

```sh
bash host/run_native_tests.sh
python3 host/compile_coeffs.py \
  --config "/absolute/path/to/your_96k_coeffs.json" \
  --header /tmp/your_generated_filters.hpp
```

The compiler validates the 96 kHz schema, band order, finite float32
coefficients and stability after float32 conversion. `build.sh` also
compiles and runs the DSP and standby native programs **using the selected
bank** before invoking the Pico SDK firmware build. The included CI recipe
builds both Pico 2 board variants from the public bank after pushing to
GitHub. A successful CI build still cannot measure the electrical DAC
outputs or receiver response.

For an end-to-end analog check, use the procedure in
[hardware and wiring](hardware_and_validation.md#first-check-on-a-new-setup).
Keep an oscilloscope or audio-interface record with the bank version,
ADC input amplitude, actual measured sample clocks, and all eight DAC
outputs if you want reproducible quantitative performance results.
