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

struct Request {
  int id;
  int type;
  std::vector<uint8_t> payload;

  Request(std::vector<uint8_t> &data) {
    this->id = i32_from_le({data[0], data[1], data[2], data[3]});
    this->type = i32_from_le({data[4], data[5], data[6], data[7]});
    this->payload = std::vector<uint8_t>(&data[8], &data[data.size() - 2]);
  }
};

Packet create_packet(int id, int type, const std::string_view data);
inline std::vector<std::vector<uint8_t>>
split_newline(std::vector<uint8_t> &data) {
  std::vector<std::vector<uint8_t>> lines;
  std::vector<uint8_t> current;

  for (auto byte : data) {
    if (byte == '\n') {
      lines.push_back(current);
      current.clear();
    } else if (byte == 0x00) {
      continue;
    } else {
      current.push_back(byte);
    }
  }

  if (!current.empty())
    lines.push_back(current);

  return lines;
}
