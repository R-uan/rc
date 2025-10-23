#pragma once

#include "relay_chat.hpp"
#include <memory>
#include <optional>

typedef std::shared_ptr<Client> ClientPtr;
// Each channel HAS an emperor and CAN HAVE up to five moderators.
// - emperor : the one that created the channel by joining it first.
// - moderators : assigned users by the emperor to have elevated privileges.
//
// If the emperor leaves the channel, the oldest moderator will take it's place.
// If there is no moderator, the channel will be destroyed.
// Emperor can manually promote a moderator to emperor, swapping their roles.
//
// If channel is secret, chatters can only join by being invited by a moderator.
// An invitation token is created by a moderator to send to a chatter.
// The invited chatter should send the token with the enter request.
struct Channel {
  std::mutex mtx;
  std::string name;
  const int MAXCAPACITY{50};
  std::atomic_bool secret{false};

  ClientPtr emperor;
  std::vector<ClientPtr> chatters{};
  std::vector<ClientPtr> moderators{};
  std::vector<std::string> invitations{};

  int leave_channel(ClientPtr &actor);                    // *
  int enter_channel(ClientPtr actor, std::string &token); // *

  int promote_emp(const ClientPtr &actor);
  int promote_emp(const ClientPtr &actor, std::string_view target);
  int promote_mod(const ClientPtr &actor, std::string_view target); // *

  // moderation
  int change_privacy(const ClientPtr &actor); // *
  int invite_chatter(const ClientPtr &actor, std::string_view target);
  int remove_chatter(const ClientPtr &actor, std::string_view target); // *

  // utils
  std::string info();                                                    // *
  bool is_authority(const ClientPtr &target);                            // *
  bool disconnect_chatter(const ClientPtr &target);                      // *
  std::optional<ClientPtr> find_chatter(const std::string_view &target); // *

  Channel(std::string n, ClientPtr creator) : emperor(creator) {
    if (n[0] != '#') {
      n = '#' + n;
    }
    this->name = n;
  }
};
