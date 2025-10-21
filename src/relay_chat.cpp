#include "relay_chat.hpp"
#include <algorithm>
#include <mutex>
#include <sys/socket.h>

bool Client::send_packet(Packet packet) {
  const auto data = packet.data.data();
  if (send(this->fd, data, sizeof(data), 0) == -1) {
    return false;
  }
  return true;
}

bool Channel::is_mod(Chatter target) {
  // check if moderator
  auto chatter =
      std::find_if(moderators.begin(), moderators.end(), [&](Chatter chatter) {
        return chatter.get() == target.get();
      });
  // is target present in this->moderators ?
  // no ? is it the emperor then ?
  return chatter != this->moderators.end()
             ? true
             : target.get() == this->emperor.get();
}

int Channel::enter_channel(Chatter actor) {
  {
    std::unique_lock lock(actor->mtx);
    actor->channels.push_back(this->name);
  }
  {
    std::unique_lock lock(this->mtx);
    this->chatters.push_back(actor);
  }
  return this->chatters.size();
}

int Channel::enter_channel(Chatter actor, std::string_view token) {
  {
    std::unique_lock lock(this->mtx);
    auto result = std::erase_if(this->invitations, [&](std::string invitation) {
      return invitation == token;
    });
    if (result == 0)
      return 0;
  }

  return this->enter_channel(actor);
}

int Channel::leave_channel(Chatter actor) {
  {
    std::unique_lock thisLock(this->mtx);
    auto result = std::erase_if(this->chatters, [&](const Chatter &client) {
      return client.get() == actor.get();
    });
    if (result == 0)
      return 0;
  }
  {
    std::unique_lock actorLock(actor->mtx);
    std::erase_if(actor->channels, [&](const std::string &channel) {
      return channel == this->name;
    });
  }

  return 0;
}

int Channel::change_privacy(Chatter actor) {
  if (!this->is_mod(actor))
    return 0;
  else {
    this->secret.exchange(!secret.load());
    return 1;
  }
}
