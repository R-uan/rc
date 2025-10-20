#include "utilities.hpp"
#include <cstring>
#include <string_view>

Packet create_packet(int32_t id, int32_t type, std::string_view data) {
  const int32_t dataSize = static_cast<int32_t>(data.size() + 10);

  std::vector<char> tempData(dataSize + 4);

  std::memcpy(tempData.data() + 0, &dataSize, sizeof(dataSize));
  std::memcpy(tempData.data() + 4, &id, sizeof(id));
  std::memcpy(tempData.data() + 8, &type, sizeof(type));
  std::memcpy(tempData.data() + 12, data.data(), data.size());
  tempData.push_back('\x00');
  tempData.push_back('\x00');

  Packet packet;
  packet.id = id;
  packet.size = dataSize;
  packet.type = type;
  packet.data = tempData;

  return packet;
}
