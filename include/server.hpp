#pragma once

#include "channel.hpp"
#include "client.hpp"
#include "managers.hpp"
#include "thread_pool.hpp"
#include <arpa/inet.h>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <shared_mutex>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

struct serversett {
  int port{3000};
  int maxChannels{15};
  int maxClients{1000};
  int dedicatedThreads{10};
};

class Server : public std::enable_shared_from_this<Server> {
private:
  int epollFd;
  int serverFd;
  std::shared_mutex epollMtx;

  int read_size(WeakClient pointer); // *
  int read_incoming(std::shared_ptr<Client> client);

  // Server Related Request Handlers
  // SVR_CONNECT handler is builtin the read_incoming
  // SRV_MESSAGE is exclusive to server -> client so it doesn't have a handler.
  void srv_disconnect(const WeakClient &client);

  // Channel Related Request Handlers
  Response ch_connect(WeakClient &client, Request &request);
  Response ch_command(const WeakClient &client, Request &request);
  Response ch_message(const WeakClient &client, Request &request);
  Response ch_disconnect(const WeakClient &client, Request &request);

public:
  std::unique_ptr<ThreadPool> threadPool;
  std::unique_ptr<ClientManager> clients;
  std::unique_ptr<ChannelManager> channels;

  Server(serversett settings) {
    this->serverFd = socket(AF_INET, SOCK_STREAM, 0);
    this->clients = std::make_unique<ClientManager>(settings.maxClients);
    this->channels = std::make_unique<ChannelManager>(settings.maxChannels);
    this->threadPool = std::make_unique<ThreadPool>(settings.dedicatedThreads);

    if (this->serverFd == -1) {
      std::cerr << "could not create server socket" << std::endl;
      exit(1);
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(settings.port);
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
    std::cout << "[DEBUG] Server ready to listen..." << std::endl;
  }

  ~Server() {
    close(this->epollFd);
    close(this->serverFd);
  }

  void listen();
  void destroy_channel(int id);
};
