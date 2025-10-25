#include "server.hpp"
#include "relay_chat.hpp"
#include "thread_pool.hpp"
#include "utilities.hpp"
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

// Utilises EPOLL to monitor new inputs on the server and client's file
// descriptors.
//
// Handles new client connections and new incoming request from already
// stablished clients.
void RcServer::listen() {
  epoll_event events[50];
  ThreadPool threadPool(10);
  while (true) {
    int nfds = epoll_wait(this->epollFd, events, 50, -1);
    for (int i = 0; i < nfds; i++) {
      int fd = events[i].data.fd;
      // handles new incoming client connections
      if (fd == this->serverFd) {
        int ncfd = accept(this->serverFd, nullptr, nullptr);
        if (ncfd != -1) {
          std::cout << "new client connected" << std::endl;
          this->add_client(ncfd);
        }
      }
      // handles new incoming client requests
      else {
        auto client = this->clients.find(fd);
        if (client == this->clients.end())
          continue;
        std::shared_ptr<Client> clientPtr = client->second;
        threadPool.enqueue([this, clientPtr]() {
          int result = this->read_incoming(clientPtr);
          // # Result can be 0 or -1
          // ##  0  : Rearms the client's event watcher
          // ## -1  : Disconnects the client
          if (result == 0) {
            epoll_event event;
            event.data.fd = clientPtr->fd;
            event.events = EPOLLIN | EPOLLONESHOT;
            {
              std::unique_lock lock(this->epollMtx);
              epoll_ctl(epollFd, EPOLL_CTL_MOD, clientPtr->fd, &event);
            }
          } else {
            this->remove_client(clientPtr);
          }
        });
      }
    }
  }
}

int RcServer::read_incoming(std::shared_ptr<Client> client) {
  int packetSize = this->read_size(client);

  if (packetSize <= 0) {
    return -1;
  }

  std::vector<uint8_t> buffer{};
  buffer.resize(packetSize);

  if (recv(client->fd, buffer.data(), packetSize, 0) <= 0) {
    return -1;
  }

  Request request(buffer);
  Response response{};
  // Check if the client has connected (sent their username)
  if (!client->connected) {
    // Check if the max client capacity has been reached
    if (this->clients.size() >= this->MAXCLIENTS) {
      response = create_response(-3, DATAKIND::CONN, "server is full");
      std::cout << "server max capacity has been reached" << std::endl;
    } else {
      if (request.type != DATAKIND::CONN) {
        response = create_response(-1, DATAKIND::CONN, "");
      } else {
        std::string nick(request.payload.begin(), request.payload.end());
        std::ostringstream oss;
        oss << nick << "@" << this->identifiers;
        auto username = oss.str();
        std::cout << username << " new client" << std::endl;
        client->username = username;
        this->identifiers.fetch_add(1);
        response = create_response(request.id, DATAKIND::CONN, username);
      }
    }
  } else {
    switch (request.type) {
    case DATAKIND::JOIN:
      response = this->handle_join(client, request);
      break;
    }
  }

  if (response.size > 0) {
    client->send_packet(response);
  }

  return 0;
}

// # The JOIN packet payload (body) will be composed of
// ## <flag> \n <channel> \n <token>
// ### <flag>    : should create channel if does not exist.
// ### <channel> : target channel's name to join.
// ### <token>   : optional: needed if the server is secret.
Response RcServer::handle_join(std::shared_ptr<Client> client,
                               Request &request) {
  auto partitions = split_newline(request.payload);
  if (partitions.size() < 2 || partitions.size() > 3) {
    return create_response(-1, DATAKIND::JOIN, "invalid packet");
  }

  bool flag = partitions[0][0] == 1;
  std::string channelName(partitions[1].begin(), partitions[1].end());

  auto it = this->channels.find(channelName);
  if (it == this->channels.end()) {
    if (!flag || this->channels.size() == this->MAXCHANNELS) {
      return create_response(-1, DATAKIND::JOIN, "does not exist");
    } else {
      auto newChannel = std::make_shared<Channel>(channelName, client);
      std::string channelInfo(newChannel->info());
      std::cout << newChannel->name << " channel created" << std::endl;
      this->channels.emplace(channelName, newChannel);
      return create_response(request.id, DATAKIND::JOIN, channelInfo);
    }
  } else {
    auto channel = it->second;
    std::string invitationToken{};
    if (partitions.size() == 3)
      invitationToken = std::string(partitions[2].begin(), partitions[2].end());

    int enter = channel->enter_channel(client, invitationToken);
    if (enter > 0) {
      client->add_channel(channelName);
      std::string channelInfo = channel->info();
      return create_response(request.id, DATAKIND::JOIN, channelInfo);
    } else if (enter == -1) {
      return create_response(-1, DATAKIND::JOIN, "channel is private");
    } else {
      return create_response(-1, DATAKIND::JOIN, "channel is full");
    }
  }
}

int RcServer::read_size(std::shared_ptr<Client> client) {
  std::vector<uint8_t> buffer{};
  buffer.resize(4);

  if (recv(client->fd, buffer.data(), 4, 0) == -1) {
    return -1;
  }

  return i32_from_le(buffer);
}

void RcServer::add_client(int fd) {
  epoll_event event;
  event.data.fd = fd;
  event.events = EPOLLIN | EPOLLONESHOT;
  {
    std::unique_lock lock(this->epollMtx);
    epoll_ctl(this->epollFd, EPOLL_CTL_ADD, fd, &event);
  }
  auto client = std::make_shared<Client>(fd);
  this->clients[fd] = std::move(client);
}

// Cleans up the client presence in the server in order to disconect
// Removes the shared pointer from the server client list
// Removes the shared pointer from the channels they're part of
// Shutsdown and closes the file descriptor
void RcServer::remove_client(const std::shared_ptr<Client> &client) {
  std::cout << client->username << " disconnected" << std::endl;
  // lets start from the bottom and move our way up.
  // the first step should be tell client's channels that they are leaving
  // for that, we will loop through client's channel list and get it's ptr
  for (std::string channelName : client->channels) {
    // we will then get the channel based on the name
    auto option = this->get_channel(channelName);
    if (!option.has_value())
      continue;

    auto channel = option.value();
    // and remove the client from that channel client pool
    channel->remove_chatter(client);
  }

  // next we will clear the client's channel pool
  {
    std::unique_lock lock(client->mtx);
    client->channels.clear();
  }

  // then we will remove the client ptr from the server's client pool
  {
    std::unique_lock lock(this->serverMtx);
    this->clients.erase(client->fd);
  }

  // at this point, the last client reference should be here so when it goes out
  // of scope it will be destroyed and the fd will be closed
}

std::optional<std::shared_ptr<Channel>>
RcServer::get_channel(const std::string &name) {
  auto result = this->channels.find(name);
  if (result != this->channels.end()) {
    return result->second;
  }
  return std::nullopt;
}
