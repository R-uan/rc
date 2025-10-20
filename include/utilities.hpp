#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

inline int i32_from_le(const std::vector<uint8_t> bytes) {
  return static_cast<int>(bytes[0] | bytes[1] << 8 | bytes[2] << 16 |
                          bytes[3] << 24);
}

struct Packet {
  int id{-1};
  int size{-1};
  int type{-1};
  std::vector<char> data{};
};

Packet create_packet(int id, int type, const std::string_view data);
