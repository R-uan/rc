#pragma once

#include "channel.hpp"
#include "managers.hpp"
#include "client.hpp"
#include "thread_pool.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <shared_mutex>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>

class Server : public std::enable_shared_from_this<Server> {
private:
  int epollFd;
  int serverFd;

  const int MAXCLIENTS;

  std::atomic_int clientIds{1};
  std::atomic_int channelIds{1};

  std::shared_mutex epollMtx;
  std::shared_mutex clientMtx;
  std::shared_mutex channelMtx;

  std::unique_ptr<ChannelManager> channels;

  int read_size(WeakClient pointer); // *
  int read_incoming(std::shared_ptr<Client> client);

  void add_client(int fd); //*

  // Server Related Request Handlers
  // SVR_CONNECT handler is builtin the read_incoming
  // SRV_MESSAGE is exclusive to server -> client so it doesn't have a handler.
  void srv_disconnect(const WeakClient &client);

  // Channel Related Request Handlers
  Response ch_connect(WeakClient &client, Request &request); // *
  Response ch_disconnect(const WeakClient &client, Request &request);

  Response ch_command(const WeakClient &client, Request &request);
  Response ch_message(const WeakClient &client, Request &request);

public:
  std::unique_ptr<ThreadPool> threadPool;
  std::unordered_map<uint32_t, std::shared_ptr<Client>> clients;

  Server(int maxClients, int maxChannels, int threads)
      : MAXCLIENTS(maxClients) {
    this->threadPool = std::make_unique<ThreadPool>(threads);
    this->channels = std::make_unique<ChannelManager>(maxChannels);

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

  ~Server() {
    close(this->epollFd);
    close(this->serverFd);
  }

  void listen();
  void destroy_channel(int id);
};
