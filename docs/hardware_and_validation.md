# Connections and first hardware check

The audio conversion design uses an externally clocked PCM1808 ADC and
four PCM5102A DACs. Wire the same LRCK (GPIO 10) and BCLK (GPIO 12) to
all five converters. Provide MCLK from GPIO 28 to the ADC; connect its
serial audio output to GPIO 27. Connect each DAC data input as follows:

| DAC | Left/right content | Data GPIO |
| --- | --- | ---: |
| 0 | LOW stereo | 11 |
| 1 | LOW_MID stereo | 13 |
| 2 | HIGH_MID stereo | 14 |
| 3 | HIGH stereo | 15 |

The W5500 uses SPI0 with MISO 0, CS 1, SCK 2 and MOSI 3; RESET is GPIO 4
and INT is GPIO 5. The W5500 is for receiver commands only. Both board
variants (`pico2`, `pico2_w`) use the same audio and W5500 pins. Match
power, signal levels, common ground, ADC data alignment and the required
converter module settings to the actual boards you use.

Before first use, edit `include/network_config.hpp`. Set unique static
addresses on your local subnet and the receiver's actual eISCP address.
The public example uses W5500 `192.168.1.159/24`, receiver
`192.168.1.158`, TCP port `60128`, and no gateway. Wi-Fi and USB data
are not needed for receiver control.

For a new crossover, inspect scope output with a low-level ADC test tone
or sweep and check all eight outputs against your intended frequency
response and channel map. Test silence from boot, continuous music,
and silence following music. If the receiver is ON and responding, the
first silence check occurs after about 300 seconds, plus the time needed
for network polling and the reply; the next check is approximately five
minutes later. If the receiver is already OFF, no standby command is sent.
The selected input does not change this behavior.

Audio processing continues during silence and when W5500 initialization
or Ethernet connection fails. There is no firmware audio mute tied to
network state. The device has no running USB serial console; BOOTSEL
flashing remains available.
