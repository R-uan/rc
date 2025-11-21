#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

struct Client;
class Server;
class Channel;

typedef std::weak_ptr<Client> WeakClient;
typedef std::weak_ptr<Server> WeakServer;

class ChannelManager {
public:
  void remove_channel(uint32_t i);
  Channel *find_channel(uint32_t i) const;
  std::vector<char> create_channel(uint32_t i, WeakClient c, WeakServer s);
  inline bool has_capacity() {
    return this->channels.size() < this->MAXCHANNELS;
  }

  ChannelManager(int max) : MAXCHANNELS(max) {};

private:
  const size_t MAXCHANNELS;
  std::shared_mutex mutex;
  std::unordered_map<uint32_t, std::unique_ptr<Channel>> channels;
};
