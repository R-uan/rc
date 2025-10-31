#pragma once

#include "channel.hpp"
#include "relay_chat.hpp"
#include "thread_pool.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <shared_mutex>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>

class RcServer : public std::enable_shared_from_this<RcServer> {
private:
  int epollFd;
  int serverFd;

  const int MAXCLIENTS;
  const int MAXCHANNELS;

  std::atomic_int clientIds;
  std::atomic_int channelIds;

  std::shared_mutex epollMtx;
  std::shared_mutex clientMtx;
  std::shared_mutex channelMtx;

  std::unordered_map<int, std::unique_ptr<Channel>> channels;

  int read_size(WeakClient pointer); // *
  int read_incoming(std::shared_ptr<Client> client);

  void add_client(int fd); //*
  void handle_disconnect(const WeakClient &client);
  Response handle_join(WeakClient &client, Request &request); // *
  std::optional<Channel *> get_channel(const std::string &name);

public:
  std::unique_ptr<ThreadPool> threadPool;
  std::unordered_map<int, std::shared_ptr<Client>> clients;

  RcServer(int mcl, int mch, int threads)
      : MAXCLIENTS(mcl), MAXCHANNELS(mch),
        threadPool(std::make_unique<ThreadPool>(threads)) {
    this->serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (this->serverFd == -1) {
      std::cerr << "could not create server socket" << std::endl;
      exit(1);
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3000);
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
    std::cout << "server has been initialized" << std::endl;
  }

  ~RcServer() {
    close(this->epollFd);
    close(this->serverFd);
  }

  void listen();
  void destroy_channel(int id);
};
