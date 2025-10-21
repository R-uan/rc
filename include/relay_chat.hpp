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

struct Channel {
  // 1 : public
  // 2 : private
  int mode{2};
  std::mutex mtx;
  int MAXCAPACITY{50};
  // channel name should start with `#` ex: #general
  std::string name;
  // whoever joins it first becomes the emperor
  // if the emperor leaves without passing the torch one of the moderators get
  // the role. If no moderator is available to become the emperor the channel is
  // destroyed.
  Chatter emperor;
  std::vector<Chatter> chatters{};
  std::array<Chatter, 5> moderator{};

  // returns relevant info about the channel (name, mode, emperor, moderators,
  // chatters) need. to figure out the formatting
  std::string info();

  // only available if mode is public
  int join(Chatter client);
  void leave(Chatter client);
  std::optional<Chatter> find_chatter(std::string username);

  // decrees (emperor operations)
  int change_mode(Chatter mod, int mode);
  // manual change, the target must be a moderator as they will change roles
  // (mod becomes emperor, emperor becomes mod)
  int promote_emperor(Chatter mod, std::string_view target);
  // happens if the emperor leaves, moderator at 0 (oldest) becomes the next
  // emperor
  int promote_emperor(Chatter mod);
  // promotes a chatter into a moderator if capacity is available
  int promote_moderator(Chatter mod, std::string_view target);

  // moderation
  int kick_chatter(Chatter mod, std::string_view target);
  int invite_chatter(Chatter mod, std::string_view target);

  Channel(std::string_view name, Chatter creator)
      : name(name), emperor(creator) {}
  static Channel create_channel(Chatter client, std::string name);
};

enum DATAKIND { CONN = 1, NICK = 2, JOIN = 3, SMSG = 4, INVI = 5, KICK = 6 };
