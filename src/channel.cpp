#include "channel.hpp"
#include "server.hpp"
#include "utilities.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

// * Enters the channel.
// - Check if the MAXCAPACITY has been reached.
// - If the channel is secret, check if the client was invited.
bool Channel::enter_channel(WeakClient actor) {
  if (this->secret) {
    if (std::erase_if(this->invitations, [&](int &invitation) {
          return invitation == actor.lock()->id;
        }) == 0)
      return false;
  }

  if (this->members.size() == this->MAXCAPACITY)
    return false;

  this->members.push_back(actor);
  return true;
}

// * Disconnects a member from the channel.
// - If the member is the emperor, promote a moderator
// - If no moderator to be promoted, the channel will be flagged for deletion
bool Channel::disconnect_member(const WeakClient &target) {
  std::unique_lock lock(this->mtx);
  if (this->emperor.lock() == target.lock()) {
    if (this->moderators.size() == 0) {
      return true;
    } else {
      auto newEmperor = this->moderators[0];
      this->moderators.erase(this->moderators.begin());
      this->emperor = newEmperor;
      // The emperor is not stored in the members/moderators vector so we can
      // just override him with the new emperor.
      return true;
    }
  }

  std::erase_if(this->members, [&](const WeakClient &member) {
    return member.lock() == target.lock();
  });

  std::erase_if(this->moderators, [&](const WeakClient &mod) {
    return mod.lock() == target.lock();
  });

  target.lock()->leave_channel(this->id);

  return false;
}

Channel::Channel(int id, WeakClient creator, WeakServer server)
    : id(id), emperor(creator), server(server) {
  std::ostringstream oss;
  oss << '#' << "channel" << id;
  this->name = oss.str();
  this->members.push_back(creator);
  std::cout << "channel [" << this->name << "] " << "was created" << std::endl;

  this->messageQueueWorkerThread = std::thread([&, this]() {
    std::unique_lock lock(this->queueMutex);
    cv.wait(lock,
            [&]() { return !queueStatus || this->messageQueue.size() > 0; });
    if (!queueStatus)
      return;

    auto server = this->server.lock();
    server->threadPool->enqueue([&, this]() {
      for (int i = 0; i < this->messageQueue.size(); i++) {
        auto packet = this->messageQueue.front();
        for (auto member : this->members) {
          if (auto client = member.lock()) {
            client->send_packet(packet);
          }
        }
        this->messageQueue.pop();
      }
    });
  });
}

Channel::~Channel() {
  std::ostringstream data;
  auto server = this->server.lock();
  data << static_cast<uint32_t>(this->id) << "server destroyed";
  auto packet = create_response(0, DATAKIND::CH_COMMAND, data.str());

  for (WeakClient pointer : this->members) {
    auto client = pointer.lock();
    client->leave_channel(this->id);
    if (client->connected) {
      server->threadPool->enqueue(
          [packet, client]() { client->send_packet(packet); });
    }
  }

  this->queueStatus.exchange(false);
  this->cv.notify_all();

  if (this->messageQueueWorkerThread.joinable()) {
    this->messageQueueWorkerThread.join();
  }

  std::cout << "channel destroyed" << std::endl;
}

std::vector<char> Channel::info() {
  uint32_t id = this->id;
  std::string name = this->name;
  uint8_t secret = this->secret ? 1 : 0;

  std::vector<char> information;
  information.reserve(5 + name.size());

  std::memcpy(information.data(), &id, sizeof(id));
  std::memcpy(information.data() + 4, &secret, 1);
  std::memcpy(information.data() + 5, name.data(), name.size());

  return information;
}

void Channel::broadcast(Response packet) {
  auto server = this->server.lock();
  server->threadPool->enqueue([&, this, packet]() {
    for (auto member : this->members) {
      if (auto client = member.lock()) {
        client->send_packet(packet);
      }
    }
  });
}

bool Channel::send_message(const WeakClient &wclient, std::string message) {
  std::vector<char> payload;
  auto client = wclient.lock();
  uint32_t channelId = this->id;
  uint32_t clientId = client->id;
  payload.resize(8 + message.size());

  std::memcpy(payload.data(), &channelId, sizeof(channelId));
  std::memcpy(payload.data() + 4, &clientId, sizeof(clientId));
  std::memcpy(payload.data() + 8, &message, message.size());

  Response packet = this->create_broadcast(DATAKIND::CH_MESSAGE, payload);
  std::unique_lock lock(this->queueMutex);
  this->messageQueue.push(packet);
  return true;
}

// UTILITIES

// Creates a response packet from a string.
Response Channel::create_broadcast(DATAKIND type, std::vector<char> data) {
  auto response = create_response(this->packetIds, type, data);
  this->packetIds.fetch_add(1);
  return response;
}

// Creates a response packet for a CH_COMMAND request.
Response Channel::create_broadcast(COMMAND command, std::string data) {
  std::vector<char> payload(data.size() + 1);
  payload[0] = command;
  std::memcpy(payload.data() + 1, data.data(), data.size());
  return this->create_broadcast(DATAKIND::CH_COMMAND, payload);
}

// Checks if the actor is a moderator or emperor
bool Channel::is_authority(const WeakClient &actor) {
  auto target = actor.lock();
  if (this->emperor.lock() == target)
    return true;
  auto it =
      std::find_if(this->moderators.begin(), this->moderators.end(),
                   [&](const WeakClient mo) { return mo.lock() == target; });
  return it != this->moderators.end();
}

// CH_COMMAND HANDLERS

// * Changes the secret status of the channel
// - Only the emperor can do this.
bool Channel::change_privacy(const WeakClient &actor) {
  if (actor.lock() == this->emperor.lock()) {
    this->secret.exchange(!this->secret);
    return true;
  }
  return false;
}

// * Kicks a member from the chanel
// - Only moderators can execute this command.
// - Only the emperor can kick other moderators.
bool Channel::kick_member(const WeakClient &actor, int target) {
  if (this->is_authority(actor)) {
    auto targetClient = std::find_if(
        this->members.begin(), this->members.end(),
        [&](const WeakClient member) { return member.lock()->id == target; });
    if (targetClient == this->members.end())
      return false;

    if (this->is_authority(*targetClient) &&
        actor.lock() != this->emperor.lock())
      return false;
    // * Changes the secret status of the channel

    return disconnect_member(*targetClient);
  }
  return false;
}

// * Invites a member to the channel.
// - If the channel is secret, only moderators can invite.
bool Channel::invite_member(const WeakClient &actor, int target) {
  if (this->secret && !this->is_authority(actor))
    return false;
  auto server = this->server.lock();
  auto newMember = server->clients.find(target);
  if (newMember != server->clients.end()) {
    this->invitations.push_back(target);
    return true;
  }
  return false;
}

// * Promote member into a moderator.
// - Only the emperor can execute this command.
bool Channel::promote_member(const WeakClient &actor, int target) {
  if (actor.lock() != this->emperor.lock() || this->moderators.size() == 5)
    return false;
  auto member = std::find_if(
      this->members.begin(), this->members.end(),
      [&](const WeakClient client) { return client.lock()->id == target; });
  if (member == this->members.end())
    return false;
  this->moderators.push_back(*member);
  return true;
}

// * Promotes a moderator into the emperor
// - Only the emperor can execute this command.
// - Member promotion will be broadcasted to the whole channel.
bool Channel::promote_moderator(const WeakClient &actor, int target) {
  if (actor.lock() != this->emperor.lock() || this->moderators.size() == 0)
    return false;
  auto moderator = std::find_if(
      this->moderators.begin(), this->moderators.end(),
      [&](const WeakClient &client) { return client.lock()->id == target; });
  if (moderator == this->members.end())
    return false;
  auto emperor = this->emperor;
  this->emperor = *moderator;
  this->moderators.push_back(emperor);
  std::erase_if(this->moderators, [&](const WeakClient &client) {
    return client.lock()->id == target;
  });
  return true;
}

// * Pins a message on the server to be seen by all members.
// - Only moderators can execute this command.
// - The message will be broadcasted to the whole channel.
bool Channel::pin_message(const WeakClient &actor, std::string message) {
  if (this->is_authority(actor)) {
    {
      std::unique_lock lock(this->mtx);
      this->pinnedMessage = message;
    }
    auto packet = this->create_broadcast(COMMAND::PIN, message);
    this->broadcast(packet);
    return true;
  }

  return false;
}

// * Changes the channel name.
// - Only the emperor can execute this.
// - The new name can have between 6-24 characters.
// - The new name will be broadcasted to the whole channel.
bool Channel::set_channel_name(const WeakClient &actor, std::string newName) {
  if (this->emperor.lock() == actor.lock()) {
    {
      std::unique_lock lock(this->mtx);
      this->name = newName;
    }
    auto packet = this->create_broadcast(COMMAND::RENAME, newName);
    this->broadcast(packet);
    return true;
  }

  return false;
}
