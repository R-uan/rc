#include "relay_chat.hpp"
#include <algorithm>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <sys/socket.h>

bool Client::send_packet(Packet packet) {
  const auto data = packet.data.data();
  if (send(this->fd, data, sizeof(data), 0) == -1) {
    return false;
  }
  return true;
}

bool Client::leave_channel(std::string_view target) {
  {
    std::unique_lock lock(this->mtx);
    auto result = std::erase_if(
        this->channels, [&](std::string channel) { return channel == target; });
    return result > 0;
  }
}

void Client::add_channel(std::string channel) {
  std::unique_lock lock(this->mtx);
  this->channels.push_back(channel);
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
  if (this->chatters.size() == this->MAXCAPACITY) {
    return 0;
  }

  {
    std::unique_lock lock(this->mtx);
    this->chatters.push_back(actor);
  }

  return this->chatters.size();
}

int Channel::enter_channel(Chatter actor, std::string &token) {
  if (this->secret) {
    if (token.size() == 0)
      return -1;
    else {
      std::unique_lock lock(this->mtx);
      auto result =
          std::erase_if(this->invitations, [&](std::string invitation) {
            return invitation == token;
          });
      if (result == 0)
        return -1;
    }
  }

  if (this->chatters.size() == this->MAXCAPACITY) {
    return 0;
  }

  {
    std::unique_lock lock(this->mtx);
    this->chatters.push_back(actor);
  }

  return this->chatters.size();
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

std::optional<Chatter> Channel::find_chatter(std::string_view target) {
  auto result = std::find_if(
      this->chatters.begin(), this->chatters.end(),
      [&](Chatter chatter) { return chatter->username == target; });
  if (result == this->chatters.end())
    return std::nullopt;
  return *result;
}

int Channel::remove_chatter(Chatter actor, std::string_view target) {
  if (this->is_mod(actor)) {
    auto chatter = this->find_chatter(target);
    if (!chatter.has_value())
      return 0;
    chatter.value()->leave_channel(this->name);
    {
      std::unique_lock lock(this->mtx);
      ssize_t result = std::erase_if(this->chatters, [&](Chatter chatter2) {
        return chatter.value().get() == chatter2.get();
      });
      if (result > 0)
        return 1;
    }
  }
  return 0;
}

int Channel::promote_moderator(Chatter mod, std::string_view target) {
  if (!this->is_mod(mod))
    return 0;
  auto chatter = this->find_chatter(target);
  if (!chatter.has_value() || this->moderators.size() >= 5)
    return 0;
  {
    std::unique_lock lock(this->mtx);
    this->moderators.push_back(chatter.value());
  }
  return 1;
}
