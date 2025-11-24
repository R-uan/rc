#include "server.hpp"
#include "channel.hpp"
#include "client.hpp"
#include "utilities.hpp"
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

// * Utilises EPOLL to monitor new inputs on the server and client's file
// descriptors.
//
// * Handles new client connections and new incoming request from already
// stablished clients.
void Server::listen() {
  std::cout << "[DEBUG] Server listening..." << std::endl;
  epoll_event events[50];
  while (true) {
    int nfds = epoll_wait(this->epollFd, events, 50, -1);
    for (int i = 0; i < nfds; i++) {
      int fd = events[i].data.fd;
      if (fd == this->serverFd) {
        int ncfd = accept(this->serverFd, nullptr, nullptr);
        if (ncfd != -1) {
          if (this->clients->has_capacity()) {
            epoll_event event;
            event.data.fd = ncfd;
            event.events = EPOLLIN | EPOLLONESHOT;
            {
              std::unique_lock lock(this->epollMtx);
              epoll_ctl(this->epollFd, EPOLL_CTL_ADD, ncfd, &event);
            }
            this->clients->add_client(ncfd);
          } else {
            std::cout << "[DEBUG] Server client capacity full" << std::endl;
            auto res = c_response(-3, DATAKIND::SVR_CONNECT, "server is full");
            auto payload = res.data;
            send(ncfd, payload.data(), payload.size(), 0);
            continue;
          }
        }
      } else {
        std::shared_lock lock(this->epollMtx);
        auto find = this->clients->find_client(fd);
        if (find != std::nullopt) {
          std::shared_ptr<Client> client = find.value();
          this->threadPool->enqueue([this, client]() {
            int result = this->read_incoming(client);
            // * Result can be 0 or -1
            // *  0 : Rearms the client's event watcher
            // * -1 : Disconnects the client
            if (result == 0) {
              epoll_event event;
              event.data.fd = client->fd;
              event.events = EPOLLIN | EPOLLONESHOT;
              std::unique_lock lock(this->epollMtx);
              epoll_ctl(epollFd, EPOLL_CTL_MOD, client->fd, &event);
            } else {
              this->srv_disconnect(client);
              epoll_ctl(this->epollFd, EPOLL_CTL_DEL, client->fd, nullptr);
            }
          });
        }
      }
    }
  }
}

// * Read incoming client packets.
// - Reads the first four bytes in the client's file descriptor for the size of
// the incoming data.
// - Resizes the buffer to match the incoming data size or disconnects the
// client if the size is lower than one.
// - Reads the rest of the data into the appropriate sized buffer.
// - Creates a Request object with the data received.
// - Checks if the client is connected, if not, all requests received will
// be treated as connection request until the client is connected.
// - After connection, pass requests down to their respective handlers and
// send back a response.
int Server::read_incoming(std::shared_ptr<Client> client) {
  int packetSize = this->read_size(client);
  if (packetSize <= 0) {
    return -1;
  }

  std::vector<uint8_t> buffer{};
  buffer.resize(packetSize);
  {
    std::unique_lock lock(client->mtx);
    if (recv(client->fd, buffer.data(), packetSize, 0) <= 0) {
      return -1;
    }
  }
  Response response{};
  Request request(buffer);
  if (!client->connected) {
    if (request.type != DATAKIND::SVR_CONNECT) {
      response = c_response(-1, DATAKIND::SVR_CONNECT, "connection needed");
    } else {
      auto payload = request.payload;
      std::string name(payload.begin(), payload.end());
      std::string newName = client->change_username(name);
      response = c_response(request.id, DATAKIND::SVR_CONNECT, newName);
      std::cout << "[DEBUG] New client: `" << newName << "`" << std::endl;
      client->change_connection(true);
    }
  } else {
    std::weak_ptr<Client> wclient = client;
    switch (request.type) {
    case DATAKIND::CH_CONNECT:
      response = this->ch_connect(wclient, request);
      break;
    case DATAKIND::CH_DISCONNECT:
      response = this->ch_disconnect(wclient, request);
      break;
    case DATAKIND::SVR_DISCONNECT:
      return -1;
      break;
    case DATAKIND::CH_MESSAGE:
      response = this->ch_message(wclient, request);
      break;
    }
  }

  if (response.size > 0) {
    client->send_packet(response);
  }

  return 0;
}

// * Reads the first four bytes on the file descriptor buffer to get the size of
// the whole request.
int Server::read_size(WeakClient pointer) {
  auto client = pointer.lock();
  std::vector<uint8_t> buffer{};
  buffer.resize(4);
  {
    std::unique_lock lock(client->mtx);
    if (recv(client->fd, buffer.data(), 4, 0) <= 0) {
      return -1;
    }
  }
  return i32_from_le(buffer);
}

// * Removes the client accross the application by lowering the shared_ptr
// counter to zero.
//
// * Possible pointer locations:
//    - Server: -> clients::unordered_map
//    - Channel -> chatters::vector
//    - Channel -> moderators::vector
//    - Channel -> emperor::shared_ptr
void Server::srv_disconnect(const WeakClient &wclient) {
  auto sclient = wclient.lock();
  sclient->connected.exchange(false);

  for (int id : sclient->channels) {
    auto channel = this->channels->find_channel(id);
    if (channel != nullptr) {
      if (channel->disconnect_member(sclient)) {
        this->channels->remove_channel(id);
      }
    }
  }

  this->clients->remove_client(sclient->fd);
  std::cout << "[DEBUG] `" << sclient->username << "` disconnected from server."
            << std::endl;
}

// CHANNEL RELATED REQUEST HANDLERS

// * Request to join a channel.
// * The JOIN packet payload will be composed of: <flag> \n <channel> \n <token>
//  - <flag>    : channel creation flag
//  - <channel> : target channel's id (int) to join.
//  - <token>   : invitation token (optional).
Response Server::ch_connect(WeakClient &client, Request &request) {
  auto body = request.payload;
  if (body.size() < 5) {
    return c_response(-1, DATAKIND::CH_CONNECT, "invalid packet");
  }

  bool flag = body[0] == 1;
  int channelId = i32_from_le({body[1], body[2], body[3], body[4]});
  auto channel = this->channels->find_channel(channelId);
  // * If the channel is not found on the server's channel pool:
  // - Check the creation flag to decide if a new channel should be created.
  // - If the flag is false or the server MAXCHANNELS number has been
  // reached: return a not found packet
  // - Otherwise create the new channel with the client as the emperor.
  if (channel == nullptr) {
    if (flag && this->channels->has_capacity()) {
      WeakClient weakClient = client;
      std::weak_ptr<Server> weakServer = weak_from_this();
      auto info = channels->create_channel(channelId, weakClient, weakServer);
      return c_response(request.id, DATAKIND::CH_CONNECT, info);
    }
    return c_response(-1, DATAKIND::CH_CONNECT);
  } else {
    if (channel->enter_channel(client)) {
      auto channelInfo = channel->info();
      auto c = client.lock();
      c->join_channel(channelId);
      std::cout << "[DEBUG] " << c->username << " joined `" << channel->name
                << "`" << std::endl;
      return c_response(request.id, DATAKIND::CH_CONNECT, channelInfo);
    }
    return c_response(-1, DATAKIND::CH_CONNECT);
  }
}

// * Disconnects the client from the channel.
// - If channel may be flagged for deletion.
Response Server::ch_disconnect(const WeakClient &sclient, Request &request) {
  if (request.payload.size() >= 4) {
    auto pl = request.payload;
    uint32_t channelId = i32_from_le({pl[0], pl[1], pl[2], pl[3]});
    auto channel = this->channels->find_channel(channelId);
    if (channel != nullptr) {
      std::cout << "[DEBUG] " << sclient.lock()->username
                << " disconnected from `" << channel->name << "`" << std::endl;
      if (channel->disconnect_member(sclient)) {
        this->channels->remove_channel(channelId);
      }
      return c_response(request.id, DATAKIND::CH_DISCONNECT);
    }
  }

  return c_response(-1, DATAKIND::CH_DISCONNECT);
}

// * Sends message in a channel.
// - Checks if the channel exists
// - Checks if the client is in the channel.
Response Server::ch_message(const WeakClient &client, Request &request) {
  const auto body = request.payload;
  const std::string message(body.begin() + 4, body.end());
  const uint32_t channelId = i32_from_le({body[0], body[1], body[2], body[3]});
  const auto channel = this->channels->find_channel(channelId);
  if (channel != nullptr) {
    if (client.lock()->is_member(channelId)) {
      channel->send_message(client, message);
      return c_response(request.id, DATAKIND::CH_MESSAGE);
    }
  }

  return c_response(-1, DATAKIND::CH_MESSAGE);
}

// * Maps command request to their respective handlers
Response Server::ch_command(const WeakClient &client, Request &request) {
  const auto body = request.payload;
  const std::string message(body.begin() + 5, body.end());
  const uint32_t channelId = i32_from_le({body[1], body[2], body[3], body[4]});
  const auto channel = this->channels->find_channel(channelId);
  const uint8_t commandId = body[0];

  if (channel != nullptr) {
    if (client.lock()->is_member(channelId)) {
      switch (commandId) {
      case COMMAND::RENAME:
        channel->set_channel_name(client, message);
        break;
      case COMMAND::PIN:
        channel->pin_message(client, message);
        break;
      }
      return c_response(request.id, DATAKIND::CH_MESSAGE, "");
    }
  }

  return c_response(-1, DATAKIND::CH_MESSAGE);
}
