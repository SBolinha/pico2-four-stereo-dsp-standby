#!/usr/bin/env bash
set -euo pipefail
readonly HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly OUT="${HERE}/build_native"
mkdir -p "${OUT}"
python3 "${HERE}/host/test_compile_coeffs.py"
python3 "${HERE}/host/compile_coeffs.py" \
  --config "${HERE}/host/fixtures/SYNTHETIC_TEST_ONLY.json" \
  --header "${OUT}/generated_filters.hpp" --allow-test-fixture
c++ -std=c++17 -O3 -Wall -Wextra -Wpedantic \
  -I"${OUT}" -I"${HERE}/include" -I"${HERE}/src" \
  "${HERE}/host/native_dsp_test.cpp" "${HERE}/src/dsp_engine.cpp" \
  -o "${OUT}/native_dsp_test"
"${OUT}/native_dsp_test"
# Also exercise the checked-in public bank through the real block engine.
python3 "${HERE}/host/compile_coeffs.py" \
  --config "${HERE}/config/example_filters_96k.json" \
  --header "${OUT}/generated_filters.hpp"
c++ -std=c++17 -O3 -Wall -Wextra -Wpedantic \
  -I"${OUT}" -I"${HERE}/include" -I"${HERE}/src" \
  "${HERE}/host/native_dsp_test.cpp" "${HERE}/src/dsp_engine.cpp" \
  -o "${OUT}/native_dsp_example_test"
"${OUT}/native_dsp_example_test"
c++ -std=c++17 -O3 -Wall -Wextra -Wpedantic \
  -I"${HERE}/src" \
  "${HERE}/host/onkyo_five_minute_off_test.cpp" \
  -o "${OUT}/onkyo_five_minute_off_test"
"${OUT}/onkyo_five_minute_off_test"
