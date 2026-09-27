#!/usr/bin/env python3
"""Generate the public, speaker-neutral 96 kHz demonstration crossover.

This is an illustrative four-band Linkwitz-Riley fourth-order bank. It does
not encode measurements, compensation or alignment for any real loudspeaker.
"""

import argparse
import json
import math
from pathlib import Path


FS = 96000
Q = 1.0 / math.sqrt(2.0)


def butterworth_section(cutoff, highpass):
    w0 = 2.0 * math.pi * cutoff / FS
    cosine, sine = math.cos(w0), math.sin(w0)
    alpha = sine / (2.0 * Q)
    if highpass:
        b0, b1, b2 = (1 + cosine) / 2, -(1 + cosine), (1 + cosine) / 2
    else:
        b0, b1, b2 = (1 - cosine) / 2, 1 - cosine, (1 - cosine) / 2
    a0 = 1 + alpha
    return dict(type="COEFF", b0=b0/a0, b1=b1/a0, b2=b2/a0,
                a1=-2*cosine/a0, a2=(1-alpha)/a0)


def lr4(cutoff, highpass):
    return [butterworth_section(cutoff, highpass) for _ in range(2)]


def band(ident, filters):
    return dict(id=ident, gain_db=-12.0, polarity=1, delay_samples=0,
                filters=filters)


def make_example():
    return dict(
        name="GENERIC_FOUR_BAND_DEMO_96K",
        sample_rate=FS,
        block_size=512,
        # Enables a functional bench-test image. This flag cannot certify
        # safety with a particular loudspeaker or amplifier.
        approved_for_hardware=True,
        input_headroom_db=-6.0,
        max_delay_samples=2048,
        bands=[
            band("LOW", lr4(250, False)),
            band("LOW_MID", lr4(250, True) + lr4(1500, False)),
            band("HIGH_MID", lr4(1500, True) + lr4(6000, False)),
            band("HIGH", lr4(6000, True)),
        ],
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).resolve().parents[1] /
                        "config/example_filters_96k.json")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(make_example(), indent=2) + "\n",
                           encoding="utf-8")
    print(f"Wrote public example: {args.output}")


if __name__ == "__main__":
    main()
