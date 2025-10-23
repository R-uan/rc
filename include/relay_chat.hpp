#pragma once

#include "utilities.hpp"
#include <atomic>
#include <mutex>
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

  bool send_packet(Response packet);
  void add_channel(std::string channel);
  bool leave_channel(std::string_view target);
};

