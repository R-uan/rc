#pragma once

#include <atomic>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

class Client;
class RcServer;
typedef std::weak_ptr<Client> WeakClient;
typedef std::weak_ptr<RcServer> WeakServer;

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
class Channel {
public:
  const int id;
  std::mutex mtx;
  std::string name;
  WeakClient emperor;

  const int MAXCAPACITY{50};

  WeakServer server;
  std::atomic_bool secret{false};

  std::vector<int> invitations{};
  std::vector<WeakClient> members{};
  std::vector<WeakClient> moderators{};

  bool enter_channel(WeakClient actor);                        // *
  bool promote_member(const WeakClient &actor, int target);    // *
  bool promote_moderator(const WeakClient &actor, int target); // *

  // moderation
  bool change_privacy(const WeakClient &actor);            // *
  bool disconnect_member(const WeakClient &target);        // *
  bool kick_member(const WeakClient &actor, int target);   // *
  bool invite_member(const WeakClient &actor, int target); // *

  // utils
  std::string info();
  void self_destroy(std::string_view reason);  // *
  bool is_authority(const WeakClient &target); // *

  Channel(int id, WeakClient creator, WeakServer server)
      : id(id), emperor(creator), server(server) {
    std::ostringstream oss;
    oss << '#' << "channel" << id;
    this->name = oss.str();
    this->members.push_back(creator);
    std::cout << "channel [" << this->name << "] " << "was created"
              << std::endl;
  }

  ~Channel();
};
