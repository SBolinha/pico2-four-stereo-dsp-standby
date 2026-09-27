import copy
import cmath
import json
import math
import sys
from pathlib import Path

from compile_coeffs import compile_config
from make_example_filters import make_example


fixture = json.loads((Path(__file__).parent / "fixtures" /
                      "SYNTHETIC_TEST_ONLY.json").read_text())


def must_fail(cfg, message):
    try:
        compile_config(cfg, allow_test_fixture=True)
    except ValueError as exc:
        if message not in str(exc):
            raise AssertionError(f"Expected {message!r}; received {exc!r}") from exc
    else:
        raise AssertionError(f"Expected rejection: {message}")


def magnitude_at(sections, frequency):
    z = cmath.exp(-2j * math.pi * frequency / 96000)
    magnitude = 1.0
    for c in sections:
        numerator = c["b0"] + c["b1"] * z + c["b2"] * z * z
        denominator = 1 + c["a1"] * z + c["a2"] * z * z
        magnitude *= abs(numerator / denominator)
    return magnitude


def main():
    header, source_block, sections = compile_config(
        fixture, allow_test_fixture=True)
    assert source_block == 64 and sections == [2, 1, 1, 1]
    assert "kSampleRate = 96000u" in header
    assert "kBlockSize = 512u" in header
    assert "kApprovedForHardware = true" in header
    assert "kBand1Sections.data(), 1u, -" in header

    bad = copy.deepcopy(fixture)
    bad["sample_rate"] = 44100
    must_fail(bad, "96 kHz")
    bad = copy.deepcopy(fixture)
    bad["approved_for_hardware"] = False
    must_fail(bad, "approved_for_hardware")
    bad = copy.deepcopy(fixture)
    bad["bands"][0]["filters"][0]["type"] = "LR4_HP"
    must_fail(bad, "all filters become COEFF")
    bad = copy.deepcopy(fixture)
    bad["bands"][0]["filters"][0]["a2"] = 1.1
    must_fail(bad, "unstable biquad")
    bad = copy.deepcopy(fixture)
    bad["bands"][2]["delay_samples"] = 101
    must_fail(bad, "exceeds max_delay_samples")

    # The published, checked-in example is reproducible and each pair of
    # adjacent fourth-order bands is near -6 dB at its crossover frequency.
    example_path = Path(__file__).parent.parent / "config/example_filters_96k.json"
    example = json.loads(example_path.read_text(encoding="utf-8"))
    assert example == make_example(), "The checked-in example differs from its generator"
    _, _, counts = compile_config(example)
    assert counts == [2, 4, 4, 2]
    for low_index, frequency in enumerate((250, 1500, 6000)):
        for index in (low_index, low_index + 1):
            amplitude = magnitude_at(example["bands"][index]["filters"], frequency)
            assert abs(amplitude - 0.5) < 0.015, (frequency, index, amplitude)

    print("PASS: sample rate, hardware approval, raw-coefficient, stability "
          "and delay guards; published 96 kHz crossover response")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise
