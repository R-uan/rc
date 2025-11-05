#include "server.hpp"
#include "channel.hpp"
#include "client.hpp"
#include "utilities.hpp"
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <ostream>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

// * Utilises EPOLL to monitor new inputs on the server and client's file
// descriptors.
//
// * Handles new client connections and new incoming request from already
// stablished clients.
void Server::listen() {
  epoll_event events[50];
  while (true) {
    int nfds = epoll_wait(this->epollFd, events, 50, -1);
    for (int i = 0; i < nfds; i++) {
      int fd = events[i].data.fd;
      if (fd == this->serverFd) {
        int ncfd = accept(this->serverFd, nullptr, nullptr);
        if (ncfd != -1) {
          this->add_client(ncfd);
        }
      } else {
        std::shared_lock lock(this->epollMtx);
        auto result = this->clients.find(fd);
        if (result != this->clients.end()) {
          std::shared_ptr<Client> client = result->second;
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
  // Check if the client has connected (sent their username)
  if (!client->connected) {
    // Check if the max client capacity has been reached
    if (this->clients.size() >= this->MAXCLIENTS) {
      response = create_response(-3, DATAKIND::SVR_CONNECT, "server is full");
    } else {
      if (request.type != DATAKIND::SVR_CONNECT) {
        response =
            create_response(-1, DATAKIND::SVR_CONNECT, "connection needed");
      } else {
        // Turns the payload into a string to get the username.
        auto payload = request.payload;
        std::string username(payload.begin(), payload.end());

        // Adds the unique identifier to the username.
        std::ostringstream handler;
        handler << username << "@" << this->clientIds;
        {
          // Sets the user handler to the client.username
          // and change client.connection state.
          std::unique_lock lock(client->mtx);
          client->username = handler.str();
          client->connected.exchange(true);
        }
        this->clientIds.fetch_add(1);
        response =
            create_response(request.id, DATAKIND::SVR_CONNECT, handler.str());
        std::cout << "[DEBUG] new client connected `" << username << "`"
                  << std::endl;
      }
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

// * Adds a new client file descriptor to the epoll.
void Server::add_client(int fd) {
  epoll_event event;
  event.data.fd = fd;
  event.events = EPOLLIN | EPOLLONESHOT;
  {
    std::unique_lock lock(this->epollMtx);
    epoll_ctl(this->epollFd, EPOLL_CTL_ADD, fd, &event);
  }
  auto client = std::make_shared<Client>(fd, this->clientIds);
  this->clients[fd] = std::move(client);
  this->clientIds.fetch_add(1);
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
  auto client = wclient.lock();
  client->connected.exchange(false);

  for (int id : client->channels) {
    auto channel = this->channels->find_channel(id);
    if (channel != nullptr) {
      if (channel->disconnect_member(client)) {
        this->channels->remove_channel(id);
      }
    }
  }

  std::unique_lock lock(this->clientMtx);
  this->clients.erase(client->fd);
  std::cout << "[DEBUG] client disconnected [" << client->username << "]"
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
    return create_response(-1, DATAKIND::CH_CONNECT, "invalid packet");
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
      return create_response(request.id, DATAKIND::CH_CONNECT, info);
    }
    return create_response(-1, DATAKIND::CH_CONNECT);
  } else {
    if (channel->enter_channel(client)) {
      auto channelInfo = channel->info();
      auto c = client.lock();
      c->join_channel(channelId);
      std::cout << "[DEBUG] " << c->username << " joined `" << channel->name
                << "`" << std::endl;
      return create_response(request.id, DATAKIND::CH_CONNECT, channelInfo);
    }
    return create_response(-1, DATAKIND::CH_CONNECT);
  }
}

Response Server::ch_disconnect(const WeakClient &client, Request &request) {
  if (request.payload.size() >= 4) {
    auto pl = request.payload;
    uint32_t channelId = i32_from_le({pl[0], pl[1], pl[2], pl[3]});
    auto channel = this->channels->find_channel(channelId);
    if (channel != nullptr) {
      if (channel->disconnect_member(client)) {
        this->channels->remove_channel(channelId);
        return create_response(request.id, DATAKIND::CH_DISCONNECT);
      }
    }
  }

  return create_response(-1, DATAKIND::CH_DISCONNECT);
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
      return create_response(request.id, DATAKIND::CH_MESSAGE);
    }
  }

  return create_response(-1, DATAKIND::CH_MESSAGE);
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
      return create_response(request.id, DATAKIND::CH_MESSAGE, "");
    }
  }

  return create_response(-1, DATAKIND::CH_MESSAGE);
}
