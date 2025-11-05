#include "client.hpp"
#include <algorithm>
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
  auto data = packet.data;
  if (send(this->fd, data.data(), data.size(), 0) == -1) {
    return false;
  }
  return true;
}

bool Client::is_member(const int channelId) {
  return std::find_if(this->channels.begin(), this->channels.end(),
                      [&](const int id) { return id == channelId; }) !=
         this->channels.end();
}
