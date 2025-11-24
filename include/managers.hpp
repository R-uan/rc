#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
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
  bool has_capacity();
  void remove_channel(uint32_t i);
  Channel *find_channel(uint32_t i) const;
  std::vector<char> create_channel(uint32_t i, WeakClient c, WeakServer s);
  ChannelManager(int max) : MAXCHANNELS(max) {};

private:
  std::shared_mutex mutex;
  const size_t MAXCHANNELS;
  std::unordered_map<uint32_t, std::unique_ptr<Channel>> channels;
};

class ClientManager {
public:
  bool has_capacity();
  void add_client(int fd);
  void remove_client(uint32_t id);
  ClientManager(int max) : MAXCLIENTS(max) {};
  std::optional<std::shared_ptr<Client>> find_client(uint32_t i) const;

private:
  const size_t MAXCLIENTS;
  std::shared_mutex mutex;
  std::atomic_int clientIds{1};
  std::unordered_map<uint32_t, std::shared_ptr<Client>> clients;
};
