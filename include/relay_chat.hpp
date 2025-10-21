#pragma once

#include "utilities.hpp"
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

struct Client {
  int fd;
  std::mutex mtx;
  std::string username;
  std::atomic_bool connected{false};
  std::vector<std::string> channels{};

  Client(int fd) {
    std::ostringstream username;
    username << "user0" << fd;

    this->username = username.str();
    this->fd = fd;
  }

  bool send_packet(Packet packet);
};

typedef std::shared_ptr<Client> Chatter;

//
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

  Chatter emperor;
  std::vector<Chatter> chatters{};
  std::array<Chatter, 5> moderators{};
  std::vector<std::string> invitations{};

  int enter_channel(Chatter actor);                         // *
  int leave_channel(Chatter actor);                         // *
  int enter_channel(Chatter actor, std::string_view token); // *

  int promote_emperor(Chatter mod);
  int promote_emperor(Chatter mod, std::string_view target);
  int promote_moderator(Chatter mod, std::string_view target);

  // moderation
  int change_privacy(Chatter actor); // *
  int remove_chatter(Chatter actor, std::string_view target);
  int invite_chatter(Chatter actor, std::string_view target);

  // utils
  std::string info();
  bool is_mod(Chatter target); // *

  Channel(std::string n, Chatter creator) : emperor(creator) {
    if (n[0] != '#') {
      n = '#' + n;
    }
    this->name = n;
  }
};

enum DATAKIND { CONN = 1, NICK = 2, JOIN = 3, SMSG = 4, INVI = 5, KICK = 6 };
