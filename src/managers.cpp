#include "managers.hpp"
#include "channel.hpp"
#include "client.hpp"
#include "server.hpp"
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <sys/types.h>
#include <utility>
#include <vector>

bool ChannelManager::has_capacity() {
  return this->MAXCHANNELS > this->channels.size();
}

std::vector<char> ChannelManager::create_channel(uint32_t i, WeakClient c,
                                                 WeakServer s) {
  auto channel = std::make_unique<Channel>(i, c, s);
  const std::vector<char> channelInfo = channel->info();
  {
    auto client = c.lock();
    std::unique_lock lock(client->mtx);
    client->channels.push_back(i);
  }
  {
    std::unique_lock lock(this->mutex);
    this->channels.emplace(i, std::move(channel));
  }
  return channelInfo;
}

void ChannelManager::remove_channel(uint32_t i) {
  std::unique_lock lock(this->mutex);
  this->channels.erase(i);
}

Channel *ChannelManager::find_channel(uint32_t i) const {
  auto find = this->channels.find(i);
  if (find == this->channels.end()) {
    return nullptr;
  }
  return find->second.get();
}

bool ClientManager::has_capacity() {
  return this->MAXCLIENTS > this->clients.size();
}

void ClientManager::add_client(int fd) {
  auto sclient = std::make_shared<Client>(fd, this->clientIds);
  this->clientIds.fetch_add(1);
  std::unique_lock lock(this->mutex);
  this->clients.emplace(fd, std::move(sclient));
}

void ClientManager::remove_client(uint32_t fd) {
  std::unique_lock lock(this->mutex);
  this->clients.erase(fd);
}

std::optional<std::shared_ptr<Client>>
ClientManager::find_client(uint32_t fd) const {
  auto find = this->clients.find(fd);
  if (find == this->clients.end()) {
    return std::nullopt;
  }
  return find->second;
}
