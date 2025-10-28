#include "server.hpp"
#include "channel.hpp"
#include "relay_chat.hpp"
#include "thread_pool.hpp"
#include "utilities.hpp"
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

// * Utilises EPOLL to monitor new inputs on the server and client's file
// descriptors.
//
// * Handles new client connections and new incoming request from already
// stablished clients.
void RcServer::listen() {
  epoll_event events[50];
  ThreadPool threadPool(10);
  while (true) {
    int nfds = epoll_wait(this->epollFd, events, 50, -1);
    for (int i = 0; i < nfds; i++) {
      int fd = events[i].data.fd;
      if (fd == this->serverFd) {
        int ncfd = accept(this->serverFd, nullptr, nullptr);
        if (ncfd != -1) {
          std::cout << "new client connected" << std::endl;
          this->add_client(ncfd);
        }
      } else {
        std::shared_lock lock(this->epollMtx);
        auto result = this->clients.find(fd);
        if (result != this->clients.end()) {
          std::shared_ptr<Client> client = result->second;
          threadPool.enqueue([this, client]() {
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
              this->remove_client(client);
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
int RcServer::read_incoming(ClientPtr client) {
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
      response = create_response(-3, DATAKIND::CONN, "server is full");
      std::cout << "server max capacity has been reached" << std::endl;
    } else {
      if (request.type != DATAKIND::CONN) {
        response = create_response(-1, DATAKIND::CONN, "connection needed");
      } else {
        // Turns the payload into a string to get the username.
        auto payload = request.payload;
        std::string username(payload.begin(), payload.end());

        // Adds the unique identifier to the username.
        std::ostringstream handler;
        handler << username << "@" << this->identifiers;
        {
          // Sets the user handler to the client.username
          // and change client.connection state.
          std::unique_lock lock(client->mtx);
          client->username = handler.str();
          client->connected.exchange(true);
        }
        this->identifiers.fetch_add(1);
        response = create_response(request.id, DATAKIND::CONN, handler.str());
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

// * The JOIN packet payload will be composed of: <flag> \n <channel> \n <token>
//  - <flag>    : channel creation flag
//  - <channel> : target channel's name to join.
//  - <token>   : invitation token (optional).
Response RcServer::handle_join(ClientPtr client, Request &request) {
  // Splits up the partitions of the body by the newline byte.
  auto partitions = split_newline(request.payload);
  if (partitions.size() < 2 || partitions.size() > 3) {
    return create_response(-1, DATAKIND::JOIN, "invalid packet");
  }

  bool flag = partitions[0][0] == 1;
  std::string channelName(partitions[1].begin(), partitions[1].end());

  auto result = this->channels.find(channelName);
  // * If the channel is not found on the server's channel pool:
  // - Check the creation flag to decide if a new channel should be created.
  // - If the flag is false or the server MAXCHANNELS number has been reached:
  // return a not found packet
  // - Otherwise create the new channel with the client as the emperor.
  if (result == this->channels.end()) {
    if (!flag || this->channels.size() == this->MAXCHANNELS) {
      return create_response(-1, DATAKIND::JOIN, "does not exist");
    } else {
      auto newChannel = std::make_shared<Channel>(channelName, client);
      std::string channelInfo(newChannel->info());
      std::cout << "channel created (" << newChannel->name << ")" << std::endl;
      {
        std::unique_lock lock(this->channelMtx);
        this->channels.emplace(newChannel->name, newChannel);
      }
      {
        std::unique_lock lock(client->mtx);
        client->channels.push_back(newChannel->name);
      }
      return create_response(request.id, DATAKIND::JOIN, channelInfo);
    }
  } else {
    auto channel = result->second;
    std::string invitationToken{};
    if (partitions.size() == 3)
      invitationToken = std::string(partitions[2].begin(), partitions[2].end());

    int enter = channel->enter_channel(client, invitationToken);
    if (enter > 0) {
      client->add_channel(channelName);
      std::string channelInfo = channel->info();
      std::cout << client->username << " joins " << channelName << std::endl;
      return create_response(request.id, DATAKIND::JOIN, channelInfo);
    } else if (enter == -1) {
      return create_response(-1, DATAKIND::JOIN, "channel is private");
    } else {
      return create_response(-1, DATAKIND::JOIN, "channel is full");
    }
  }
}

// * Reads the first four bytes on the file descriptor buffer to get the size of
// the whole request.
int RcServer::read_size(std::shared_ptr<Client> client) {
  std::vector<uint8_t> buffer{};
  buffer.resize(4);
  {
    std::unique_lock lock(client->mtx);
    if (recv(client->fd, buffer.data(), 4, 0) == -1) {
      return -1;
    }
  }
  return i32_from_le(buffer);
}

// * Adds a new client file descriptor to the epoll.
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

// * Removes the client accross the application by lowering the shared_ptr
// counter to zero.
//
// * Possible pointer locations:
//    - RcServer: -> clients::unordered_map
//    - Channel -> chatters::vector
//    - Channel -> moderators::vector
//    - Channel -> emperor::shared_ptr
void RcServer::remove_client(const std::shared_ptr<Client> &client) {
  for (std::string channelName : client->channels) {
    auto option = this->get_channel(channelName);
    if (option.has_value()) {
      auto channel = option.value();
      // * If true, the channel was flagged for deletion.
      if (channel->disconnect_member(client)) {
        this->channels.erase(channelName);
      }
    }
  }

  std::unique_lock lock(this->clientMtx);
  this->clients.erase(client->fd);
}

// Not sure if this function is necessary but it will remain as for now.
std::optional<std::shared_ptr<Channel>>
RcServer::get_channel(const std::string &name) {
  auto result = this->channels.find(name);
  if (result != this->channels.end()) {
    return result->second;
  }
  return std::nullopt;
}
