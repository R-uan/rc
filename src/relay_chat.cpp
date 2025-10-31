#include "relay_chat.hpp"
#include <mutex>
#include <sys/socket.h>

void Client::join_channel(const int channelId) {
  std::unique_lock lock(this->mtx);
  this->channels.push_back(channelId);
}

void Client::leave_channel(const int channelId) {
  std::unique_lock lock(this->mtx);
  std::erase_if(this->channels,
                [&](const int &channel) { return channel == channelId; });
}

bool Client::send_packet(const Response packet) {
  auto data = packet.data.data();
  if (send(this->fd, data, sizeof(data), 0) == -1) {
    return false;
  }
  return true;
}
