#include "relay_chat.hpp"
#include <mutex>
#include <string>
#include <string_view>
#include <sys/socket.h>

bool Client::send_packet(Response packet) {
  const auto data = packet.data.data();
  if (send(this->fd, data, sizeof(data), 0) == -1) {
    return false;
  }
  return true;
}

bool Client::leave_channel(std::string_view target) {
  {
    std::unique_lock lock(this->mtx);
    auto result =
        std::erase_if(this->channels, [&](const std::string &channel) {
          return channel == target;
        });
    return result > 0;
  }
}

void Client::add_channel(std::string channel) {
  std::unique_lock lock(this->mtx);
  this->channels.push_back(channel);
}
