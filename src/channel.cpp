#include "channel.hpp"
#include <algorithm>
#include <sstream>

// Attempts to join channel
// - If the channel is secret a token of size bigger than zero is needed.
// - Checks if the channel's MAXCAPACITY has been reached.
// - Checks if the given token is present on the issued invitations vector.
int Channel::enter_channel(ClientPtr actor, std::string &token) {
  if (this->chatters.size() == this->MAXCAPACITY) {
    return 0;
  }

  if (this->secret) {
    if (token.size() <= 0)
      return -1;
    {
      std::unique_lock lock(this->mtx);
      auto result = std::erase_if(invitations, [&](std::string invitation) {
        return invitation == token;
      });

      if (result == 0)
        return -1;
    }
  }

  {
    std::unique_lock lock(this->mtx);
    this->chatters.push_back(std::move(actor));
  }

  return this->chatters.size();
}

int Channel::leave_channel(ClientPtr &actor) {
  {
    std::unique_lock thisLock(this->mtx);
    auto result = std::erase_if(this->chatters, [&](const ClientPtr &client) {
      return client == actor;
    });
    if (result == 0)
      return -1;
  }
  {
    std::unique_lock actorLock(actor->mtx);
    std::erase_if(actor->channels, [&](const std::string &channel) {
      return channel == this->name;
    });
  }

  return 0;
}

// # Switch for the privacy of the channel.
// # Can only be changed by the emperor.
int Channel::change_privacy(const ClientPtr &actor) {
  if (!(this->emperor == actor))
    return -1;
  else {
    this->secret.exchange(!secret.load());
    return 1;
  }
}

// # Tries to find the target by the username and returns it's shared_ptr if
// found
std::optional<ClientPtr> Channel::find_chatter(const std::string_view &target) {
  auto result = std::find_if(
      this->chatters.begin(), this->chatters.end(),
      [&](const ClientPtr &chatter) { return chatter->username == target; });
  if (result == this->chatters.end())
    return std::nullopt;
  return *result;
}

// # Mod/Emperor to remove chatter from the channel
// ## Check if the actor has the authority
// ## Looks for the target chatter
// ## Force disconnect the chatter
// ## Removes them from the channel's list
int Channel::remove_chatter(const ClientPtr &actor, std::string_view target) {
  if (!this->is_authority(actor))
    return -1;

  auto result = this->find_chatter(target);
  if (result == std::nullopt)
    return -1;

  auto chatter = result.value();
  chatter->leave_channel(this->name);
  this->disconnect_chatter(chatter);
  return 1;
}

// # Removes the target chatter from the channel's chatter list
bool Channel::disconnect_chatter(const ClientPtr &target) {
  std::unique_lock lock(this->mtx);
  return std::erase_if(this->chatters,
                       [&](ClientPtr chatter) { return chatter == target; });
}
// # Emperor manually promotes a moderator to emperor
// # The emperor will become a moderator and the target moderator will become
// the emperor.
// # The target can only be a moderator.
int Channel::promote_mod(const ClientPtr &actor, std::string_view target) {
  return 0;
}

std::string Channel::info() {
  std::ostringstream info;
  info << this->name << '\n';
  info << this->chatters.size() << '\n';
  return info.str();
}

bool Channel::is_authority(const ClientPtr &target) {
  auto m = this->moderators;
  auto chatter =
      std::find_if(m.begin(), m.end(),
                   [&](const ClientPtr &chatter) { return chatter == target; });
  return chatter != m.end() ? true : target.get() == this->emperor.get();
}
