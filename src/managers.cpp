#include "managers.hpp"
#include "channel.hpp"
#include "server.hpp"
#include <memory>
#include <utility>
#include <vector>

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
  std::cout << "remove" << std::endl;
  std::cout << this->channels.size() << std::endl;
  try {
    this->channels.erase(i);
  } catch (const std::exception &e) {
    std::cerr << "Exception during erase: " << e.what() << std::endl;
  }
  std::cout << this->channels.size() << std::endl;
}

Channel *ChannelManager::find_channel(uint32_t i) const {
  auto find = this->channels.find(i);
  if (find == this->channels.end()) {
    return nullptr;
  }
  return find->second.get();
}
