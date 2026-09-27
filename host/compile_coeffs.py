#!/usr/bin/env python3
"""Validate local 96 kHz COEFF JSON and generate the firmware C++ header.

Uses only the Python standard library. Never prints or uploads filter values.
"""

import argparse
import cmath
import json
import math
import struct
from pathlib import Path


def integer(value, label, minimum=0):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{label} must be an integer")
    if not math.isfinite(value) or int(value) != value or value < minimum:
        raise ValueError(f"{label} must be an integer >= {minimum}")
    return int(value)


def f32(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{label} must be a finite number")
    if not math.isfinite(value):
        raise ValueError(f"{label} must be finite")
    try:
        rounded = struct.unpack("<f", struct.pack("<f", value))[0]
    except OverflowError as exc:
        raise ValueError(f"{label} overflows float32") from exc
    if not math.isfinite(rounded):
        raise ValueError(f"{label} overflows float32")
    return rounded


def literal(value):
    s = f"{value:.9g}"
    if "." not in s and "e" not in s.lower():
        s += ".0"
    return s + "f"


def compile_config(cfg, allow_test_fixture=False):
    if not isinstance(cfg, dict):
        raise ValueError("Root JSON value must be an object")
    name = cfg.get("name")
    if not isinstance(name, str) or not name.strip():
        raise ValueError("Configuration needs a nonempty name")
    if not allow_test_fixture and any(
            word in name.upper() for word in ("EXAMPLE", "TEMPLATE", "SYNTHETIC")):
        raise ValueError("Example settings cannot be used for a firmware build")
    if integer(cfg.get("sample_rate"), "sample_rate") != 96000:
        raise ValueError("Use the notebook's computed 96 kHz JSON, not 44.1 kHz coefficients")
    source_block_size = integer(cfg.get("block_size"), "block_size", 1)
    if cfg.get("approved_for_hardware") is not True:
        raise ValueError("Set approved_for_hardware=true in your validated JSON")
    if "max_delay_ms" in cfg:
        raise ValueError("Use max_delay_samples, not max_delay_ms, for this build")
    maximum_delay = integer(cfg.get("max_delay_samples"), "max_delay_samples")
    headroom = f32(cfg.get("input_headroom_db", 0), "input_headroom_db")
    headroom_lin = f32(10 ** (headroom / 20), "input_headroom")
    bands = cfg.get("bands")
    if not isinstance(bands, list) or len(bands) != 4 or [
            band.get("id") if isinstance(band, dict) else None for band in bands
            ] != ["LOW", "LOW_MID", "HIGH_MID", "HIGH"]:
        raise ValueError("Expected exactly four bands, ordered W, LM, UM, T")

    tables = []
    longest = 0
    for band in bands:
        ident = band["id"]
        gain_db = f32(band.get("gain_db", 0), f"{ident}.gain_db")
        if band.get("polarity", 1) not in (-1, 1) or isinstance(
                band.get("polarity", 1), bool):
            raise ValueError(f"{ident}.polarity must be +1 or -1")
        polarity = int(band.get("polarity", 1))
        if "delay_ms" in band:
            raise ValueError(f"{ident}: use delay_samples, not delay_ms")
        delay = integer(band.get("delay_samples", 0), f"{ident}.delay_samples")
        if delay > maximum_delay:
            raise ValueError(f"{ident}.delay_samples exceeds max_delay_samples")
        sections = band.get("filters")
        if not isinstance(sections, list) or not sections:
            raise ValueError(f"{ident}: expected at least one COEFF filter")
        output = []
        for section_number, section in enumerate(sections, 1):
            label = f"{ident}.filters[{section_number}]"
            if not isinstance(section, dict) or section.get("type", "").upper() != "COEFF":
                raise ValueError(f"{label}: run the notebook so all filters become COEFF")
            c = tuple(f32(section.get(k), label + "." + k)
                      for k in ("b0", "b1", "b2", "a1", "a2"))
            # The firmware recurrence uses denominator 1+a1*z^-1+a2*z^-2.
            discriminant = cmath.sqrt(complex(c[3]*c[3] - 4*c[4]))
            roots = ((-c[3] + discriminant)/2,
                     (-c[3] - discriminant)/2)
            if max(abs(root) for root in roots) >= 1:
                raise ValueError(f"{label}: unstable biquad after float32 rounding")
            output.append(c)
        signed_gain = f32((10 ** (gain_db / 20))*polarity,
                          f"{ident}.signed_gain")
        tables.append((ident, delay, signed_gain, output))
        longest = max(longest, len(output))

    lines = [
        "// Generated locally from user-owned 96 kHz JSON. Do not hand-edit.",
        "#pragma once",
        "#include <array>",
        "#include <cstddef>",
        "#include <cstdint>",
        '#include "dsp_types.hpp"',
        "namespace dsp4::generated {",
        "inline constexpr const char* kConfigurationName = " + json.dumps(name) + ";",
        "inline constexpr std::uint32_t kSampleRate = 96000u;",
        "inline constexpr std::size_t kBlockSize = 512u;",
        f"inline constexpr std::size_t kMaxDelaySamples = {maximum_delay}u;",
        f"inline constexpr std::size_t kMaxSections = {longest}u;",
        "inline constexpr bool kApprovedForHardware = true;",
        f"inline constexpr float kInputHeadroom = {literal(headroom_lin)};",
    ]
    for idx, (ident, delay, gain, sections) in enumerate(tables):
        lines.append(
            f"inline constexpr std::array<BiquadCoeffs, kMaxSections> kBand{idx}Sections = {{{{")
        for sec in sections:
            lines.append("    {" + ", ".join(map(literal, sec)) + "},")
        for _ in range(longest - len(sections)):
            lines.append("    {1.0f, 0.0f, 0.0f, 0.0f, 0.0f},")
        lines.extend([
            "}};",
            f"inline constexpr BandDefinition kBand{idx} = {{",
            f"    kBand{idx}Sections.data(), {len(sections)}u, {literal(gain)},",
            f"    {delay}u, {json.dumps(ident)}}};",
        ])
    lines.append("inline constexpr std::array<BandDefinition, 4> kBands = {{")
    lines.extend(f"    kBand{i}," for i in range(4))
    lines.extend(["}};", "}  // namespace dsp4::generated", ""])
    return "\n".join(lines), source_block_size, [len(x[3]) for x in tables]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--header", required=True, type=Path)
    parser.add_argument("--allow-test-fixture", action="store_true")
    args = parser.parse_args()
    with args.config.open("r", encoding="utf-8") as f:
        cfg = json.load(f)
    header, block_size, section_counts = compile_config(
        cfg, allow_test_fixture=args.allow_test_fixture)
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text(header, encoding="utf-8")
    print("96 kHz filter bank validated locally; biquads LOW/LOW_MID/HIGH_MID/HIGH:",
          "/".join(map(str, section_counts)))
    if block_size != 512:
        print(f"The input block_size={block_size} is metadata; the 512-frame "
              "DMA engine uses kBlockSize=512. Coefficients are unchanged.")


if __name__ == "__main__":
    main()
