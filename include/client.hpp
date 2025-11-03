#pragma once

#include "utilities.hpp"
#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

// Shared Pointer Tracker (Where a client shared_ptr can be found)
// # Server
//   -> client unordered map
// # Channel
//   -> chatter vector
//   -> moderator vector
//   -> emperor
//
struct Client {
  int fd;
  int id;
  std::mutex mtx;
  std::string username;
  std::vector<uint32_t> channels{};
  std::atomic_bool connected{false};

  Client(int fd, int id) {
    std::ostringstream username;
    username << "user0" << fd;

    this->username = username.str();
    this->fd = fd;
    this->id = id;
  }

  ~Client() {
    std::cout << this->username << " destroyed" << std::endl;
    close(this->fd);
  }

  void join_channel(const int channelId);
  void leave_channel(const int channelId);
  bool send_packet(const Response packet);
  bool is_member(const int channelId);
};

typedef std::shared_ptr<Client> SharedClient;
typedef std::weak_ptr<Client> WeakClient;
