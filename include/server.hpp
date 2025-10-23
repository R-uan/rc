#pragma once

#include "channel.hpp"
#include "relay_chat.hpp"
#include "utilities.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>

class RcServer {
private:
  int epollFd;
  int serverFd;

  // server capacity
  const int MAXCLIENTS;
  const int MAXCHANNELS;
  // used in the creation of username to guarantee uniqueness
  std::atomic_int identifiers;

  std::mutex epollMtx;
  std::mutex serverMtx;

  // client keys are their file descriptor
  std::unordered_map<int, std::shared_ptr<Client>> clients;
  // channel keys should start with `#`, ex: #general
  std::unordered_map<std::string, std::shared_ptr<Channel>> channels;

  int read_size(std::shared_ptr<Client> client); // *
  int read_incoming(std::shared_ptr<Client> client);

  void add_client(int fd); //*
  void remove_client(std::shared_ptr<Client> client);
  Response handle_join(std::shared_ptr<Client> client, Request &request); // *

  RcServer(int mcl, int mch) : MAXCLIENTS(mcl), MAXCHANNELS(mch) {
    this->serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (this->serverFd == -1) {
      std::cerr << "could not create server socket" << std::endl;
      exit(1);
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (bind(this->serverFd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
      std::cerr << "server could not be initialized on given addr" << std::endl;
      close(this->serverFd);
      exit(2);
    }

    if (::listen(this->serverFd, SOMAXCONN) == -1) {
      std::cerr << "could not start listening to socket" << std::endl;
      close(this->serverFd);
      exit(3);
    }

    epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = this->serverFd;
    this->epollFd = epoll_create1(0);
    epoll_ctl(this->epollFd, EPOLL_CTL_ADD, this->serverFd, &ev);
  }

  ~RcServer() {
    close(this->epollFd);
    close(this->serverFd);
  }

public:
  void listen();
};
