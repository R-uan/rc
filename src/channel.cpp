#include "channel.hpp"
#include <algorithm>
#include <sstream>

// * Attempts to join channel
// - If the channel is secret a token of size bigger than zero is needed.
// - Checks if the channel's MAXCAPACITY has been reached.
// - Checks if the given token is present on the issued invitations vector.
int Channel::enter_channel(ClientPtr actor, std::string &token) {
  if (this->members.size() == this->MAXCAPACITY) {
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
    this->members.push_back(std::move(actor));
  }

  return this->members.size();
}

// * Switch for the privacy of the channel.
// * Can only be changed by the emperor.
bool Channel::change_privacy(const ClientPtr &actor) {
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
      this->members.begin(), this->members.end(),
      [&](const ClientPtr &chatter) { return chatter->username == target; });
  if (result == this->members.end())
    return std::nullopt;
  return *result;
}

// # Mod/Emperor to remove chatter from the channel
// ## Check if the actor has the authority
// ## Looks for the target chatter
// ## Force disconnect the chatter
// ## Removes them from the channel's list
bool Channel::kick_member(const ClientPtr &actor, std::string_view target) {
  if (!this->is_authority(actor))
    return -1;

  auto result = this->find_chatter(target);
  if (result == std::nullopt)
    return -1;

  auto member = result.value();
  member->remove_channel(this->name);
  this->disconnect_member(member);
  return 1;
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
  info << this->members.size() << '\n';
  return info.str();
}

bool Channel::is_authority(const ClientPtr &target) {
  auto m = this->moderators;
  auto chatter =
      std::find_if(m.begin(), m.end(),
                   [&](const ClientPtr &chatter) { return chatter == target; });
  return chatter != m.end() ? true : target.get() == this->emperor.get();
}

// * Totally removes a chatter from the channel.
// * If the chatter is the emperor, give the ownership of the channel
// to the oldest mod. Otherwise marks the channel for deletion.
bool Channel::disconnect_member(const ClientPtr &target) {
  bool deletionFlag = false;
  std::unique_lock lock(this->mtx);
  if (this->emperor == target) {
    if (this->moderators.size() == 0) {
      // * Because the channel is flagged to be deleted
      // it's safe to nullptr this (I think (I hope)).
      this->emperor = nullptr;
      deletionFlag = true;
    } else {
      // Transfers ownership to the oldest moderator.
      ClientPtr newEmperor = this->moderators[0];
      this->moderators.erase(this->moderators.begin());
      this->emperor = newEmperor;
    }
  }

  std::erase_if(this->moderators,
                [&](const ClientPtr &client) { return client == target; });

  std::erase_if(this->members,
                [&](const ClientPtr &client) { return client == target; });

  return deletionFlag;
}
