cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED ENV{PICO_SDK_PATH})
    message(FATAL_ERROR "Set PICO_SDK_PATH to a Pico SDK checkout with submodules")
endif()
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico2_four_stereo_dsp_standby C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

if(NOT PICO_BOARD STREQUAL "pico2" AND NOT PICO_BOARD STREQUAL "pico2_w")
    message(FATAL_ERROR "This project targets PICO_BOARD=pico2 or pico2_w")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/generated_filters.hpp")
    message(FATAL_ERROR "Generate the filter header with build.sh first")
endif()

add_executable(pico2_four_stereo_dsp_standby
    src/main.cpp
    src/dsp_engine.cpp
    src/w5500_onkyo.cpp)

pico_generate_pio_header(pico2_four_stereo_dsp_standby
    ${CMAKE_CURRENT_LIST_DIR}/src/audio_transport.pio)
target_include_directories(pico2_four_stereo_dsp_standby PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/include)
target_compile_options(pico2_four_stereo_dsp_standby PRIVATE
    -O3 -Wall -Wextra -Wpedantic)
target_link_options(pico2_four_stereo_dsp_standby PRIVATE
    -Wl,--print-memory-usage
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/pico2_four_stereo_dsp_standby.map)
target_link_libraries(pico2_four_stereo_dsp_standby PRIVATE
    pico_stdlib pico_multicore
    hardware_clocks hardware_dma hardware_gpio hardware_irq
    hardware_pio hardware_spi)

pico_enable_stdio_usb(pico2_four_stereo_dsp_standby 0)
pico_enable_stdio_uart(pico2_four_stereo_dsp_standby 0)
pico_add_extra_outputs(pico2_four_stereo_dsp_standby)
