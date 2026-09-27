#pragma once

#include <cstdint>

// Example static addresses for a W5500 on a local /24 network. Set these
// to unused addresses on your own network before connecting the receiver.
namespace network_config {
inline constexpr std::uint8_t mac[6] = {0x02, 0x35, 0x00, 0x00, 0x01, 0x59};
inline constexpr std::uint8_t device_ip[4] = {192, 168, 1, 159};
inline constexpr std::uint8_t mask[4] = {255, 255, 255, 0};
inline constexpr std::uint8_t gateway[4] = {0, 0, 0, 0};
inline constexpr std::uint8_t receiver_ip[4] = {192, 168, 1, 158};
inline constexpr std::uint16_t receiver_port = 60128;
}  // namespace network_config
