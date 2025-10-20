#include "relay_chat.hpp"
#include "server.hpp"
#include "thread_pool.hpp"
#include "utilities.hpp"
#include <cstdint>
#include <memory>
#include <mutex>
#include <sstream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <vector>

void RcServer::listen() {
  epoll_event events[50];
  ThreadPool threadPool(10);
  while (true) {
    int nfds = epoll_wait(this->epollFd, events, 50, -1);
    for (int i = 0; i < nfds; i++) {
      int fd = events[i].data.fd;
      if (fd == this->serverFd) {
        // Handles new client connection on the server.
        int ncfd = accept(this->serverFd, nullptr, nullptr);
        if (ncfd != -1) {
          this->add_client(ncfd);
        }
      } else {
        auto client = this->clients.find(fd);
        if (client == this->clients.end())
          continue;
        std::shared_ptr<Client> clientPtr = client->second;
        threadPool.enqueue([this, clientPtr]() {
          int result = this->read_incoming(clientPtr);
          // Result can be 0 or -1
          // * 0  : Re-arms the fd events
          // * -1 : Disconnects the client
          if (result == 0) {
            epoll_event event;
            event.data.fd = clientPtr->fd;
            event.events = EPOLLIN | EPOLLONESHOT;
            {
              std::unique_lock lock(this->epollMtx);
              epoll_ctl(epollFd, EPOLL_CTL_MOD, clientPtr->fd, &event);
            }
          } else {
            this->rmv_client(clientPtr);
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

  std::string packetBody(&buffer[8], &buffer[buffer.size() - 2]);
  int pid = i32_from_le({buffer[0], buffer[1], buffer[2], buffer[3]});
  int type = i32_from_le({buffer[4], buffer[5], buffer[6], buffer[7]});

  Packet responsePacket{};
  if (!client->connected) {
    // Check if the max client capacity has been reached
    if (this->clients.size() >= this->MAXCLIENTS) {
      responsePacket = create_packet(-3, DATAKIND::CONN, "server is full");
      std::cout << "server max capacity has been reached" << std::endl;
      client->send_packet(responsePacket);
      return -1;
    }

    if (type != DATAKIND::CONN) {
      responsePacket = create_packet(-1, DATAKIND::CONN, "");
    } else {
      // Right now CONN only sets the client's username without any
      // authentication I'll decide if I want to expand it into an actual
      // authentication system
      if (packetBody.size() > 14) {
        responsePacket = create_packet(-2, DATAKIND::CONN, "");
      } else {
        // username example: bunny@23
        std::ostringstream username;
        username << packetBody << "@" << this->identifiers;
        std::string usernameStr = username.str();
        client->username = usernameStr;
        this->identifiers.fetch_add(1);
        responsePacket = create_packet(pid, DATAKIND::CONN, usernameStr);
      }
    }
  }

  if (responsePacket.size > 0) {
    client->send_packet(responsePacket);
  }

  return 0;
}

int RcServer::read_size(std::shared_ptr<Client> client) {
  std::vector<uint8_t> buffer{};
  buffer.resize(4);

  if (recv(client->fd, &buffer, 4, 0) == -1) {
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
