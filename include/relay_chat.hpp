#pragma once

#include "utilities.hpp"
#include <atomic>
#include <iostream>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>
// Shared Pointer Tracker (Where a client shared_ptr can be found)
// # RcServer
//   -> client unordered map
// # Channel
//   -> chatter vector
//   -> moderator vector
//   -> emperor
//
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

  ~Client() {
    std::cout << this->username << " destroyed" << std::endl;
    close(this->fd);
  }

  bool send_packet(const Response packet);
  void add_channel(const std::string_view &channel);
  bool remove_channel(const std::string_view &target);
};
