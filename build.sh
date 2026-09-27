#!/usr/bin/env bash
set -euo pipefail

readonly HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly BOARD="${PICO_BOARD:-pico2}"
readonly FILTER_JSON="${DSP_FILTER_JSON:-${HERE}/config/example_filters_96k.json}"

case "${BOARD}" in
  pico2|pico2_w) ;;
  *) echo "PICO_BOARD must be pico2 or pico2_w" >&2; exit 1 ;;
esac
if [[ ! -f "${FILTER_JSON}" ]]; then
  echo "DSP_FILTER_JSON must point to a 96 kHz COEFF JSON file." >&2
  exit 1
fi
if [[ -z "${PICO_SDK_PATH:-}" || ! -f "${PICO_SDK_PATH}/external/pico_sdk_import.cmake" ]]; then
  echo "Set PICO_SDK_PATH to a Pico SDK checkout with submodules." >&2
  exit 1
fi

readonly BUILD="${HERE}/build_${BOARD}"
mkdir -p "${BUILD}"
python3 "${HERE}/host/compile_coeffs.py" \
    --config "${FILTER_JSON}" --header "${BUILD}/generated_filters.hpp"

# Run DSP checks with the same generated bank before the firmware build.
c++ -std=c++17 -O3 -Wall -Wextra -Wpedantic \
    -I"${BUILD}" -I"${HERE}/include" -I"${HERE}/src" \
    "${HERE}/host/native_dsp_test.cpp" "${HERE}/src/dsp_engine.cpp" \
    -o "${BUILD}/native_dsp_test"
"${BUILD}/native_dsp_test"
c++ -std=c++17 -O3 -Wall -Wextra -Wpedantic \
    -I"${HERE}/src" "${HERE}/host/onkyo_five_minute_off_test.cpp" \
    -o "${BUILD}/onkyo_five_minute_off_test"
"${BUILD}/onkyo_five_minute_off_test"

cmake -S "${HERE}" -B "${BUILD}" \
    -DPICO_BOARD="${BOARD}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD}" -j4
cp "${BUILD}/pico2_four_stereo_dsp_standby.uf2" \
   "${HERE}/pico2_four_stereo_dsp_standby_${BOARD}.uf2"
echo "Built: ${HERE}/pico2_four_stereo_dsp_standby_${BOARD}.uf2"
