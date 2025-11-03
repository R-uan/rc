#include "utilities.hpp"
#include <cstdint>
#include <cstring>
#include <string_view>

int i32_from_le(const std::vector<uint8_t> bytes) {
  return static_cast<int>(bytes[0] | bytes[1] << 8 | bytes[2] << 16 |
                          bytes[3] << 24);
}

Response create_response(const int32_t id, const uint32_t type,
                         const std::string_view data) {
  const int32_t dataSize = static_cast<int32_t>(data.size() + 10);

  std::vector<char> tempData(dataSize + 4);

  std::memcpy(tempData.data() + 0, &dataSize, sizeof(dataSize));
  std::memcpy(tempData.data() + 4, &id, sizeof(id));
  std::memcpy(tempData.data() + 8, &type, sizeof(type));
  std::memcpy(tempData.data() + 12, data.data(), data.size());
  tempData.push_back('\x00');
  tempData.push_back('\x00');

  Response packet;
  packet.id = id;
  packet.size = dataSize;
  packet.type = type;
  packet.data = tempData;

  return packet;
}

Response create_response(const int32_t id, const uint32_t type,
                         const std::vector<uint32_t> data) {
  const int32_t dataSize = static_cast<int32_t>(data.size() + 10);

  std::vector<char> tempData(dataSize + 4);

  std::memcpy(tempData.data() + 0, &dataSize, sizeof(dataSize));
  std::memcpy(tempData.data() + 4, &id, sizeof(id));
  std::memcpy(tempData.data() + 8, &type, sizeof(type));
  std::memcpy(tempData.data() + 12, data.data(), data.size());
  tempData.push_back('\x00');
  tempData.push_back('\x00');

  Response packet;
  packet.id = id;
  packet.size = dataSize;
  packet.type = type;
  packet.data = tempData;

  return packet;
}

Response create_response(const int32_t id, const uint32_t type) {
  const int32_t dataSize = static_cast<int32_t>(10);

  std::vector<char> tempData(dataSize + 4);

  std::memcpy(tempData.data() + 0, &dataSize, sizeof(dataSize));
  std::memcpy(tempData.data() + 4, &id, sizeof(id));
  std::memcpy(tempData.data() + 8, &type, sizeof(type));
  tempData.push_back('\x00');
  tempData.push_back('\x00');

  Response packet;
  packet.id = id;
  packet.size = dataSize;
  packet.type = type;
  packet.data = tempData;

  return packet;
}

Response create_response(const int32_t id, const uint32_t type,
                         const std::vector<char> data) {
  const int32_t dataSize = static_cast<int32_t>(data.size() + 10);

  std::vector<char> tempData(dataSize + 4);

  std::memcpy(tempData.data() + 0, &dataSize, sizeof(dataSize));
  std::memcpy(tempData.data() + 4, &id, sizeof(id));
  std::memcpy(tempData.data() + 8, &type, sizeof(type));
  std::memcpy(tempData.data() + 12, data.data(), data.size());
  tempData.push_back('\x00');
  tempData.push_back('\x00');

  Response packet;
  packet.id = id;
  packet.size = dataSize;
  packet.type = type;
  packet.data = tempData;

  return packet;
}

std::vector<std::vector<uint8_t>> split_newline(std::vector<uint8_t> &data) {
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
