#pragma once
#include <cstdint>

///
/// @param address
/// @param prefix_bits
/// @param send_port Port to send data
/// @param recv_port Port to listen to, receiving data
/// @return
bool start_tun(const char *address, uint8_t prefix_bits, uint16_t send_port, uint16_t recv_port);
