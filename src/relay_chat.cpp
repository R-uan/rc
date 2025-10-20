#include "relay_chat.hpp"
#include <sys/socket.h>

bool Client::send_packet(Packet packet) {
  const auto data = packet.data.data();
  if (send(this->fd, data, sizeof(data), 0) == -1) {
    return false;
  }
  return true;
}

std::optional<Chatter> Channel::find_chatter(std::string username) {
  for (int i = 0; i < this->chatters.size(); i++) {
    auto chatter = this->chatters[i];
    if (chatter->username == username) {
      return chatter;
    }
  }

  return std::nullopt;
}
