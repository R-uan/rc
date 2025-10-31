#include "channel.hpp"
#include "server.hpp"
#include "utilities.hpp"
#include <algorithm>
#include <sstream>
#include <string_view>

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

  return true;
}

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
// - Only authority can do it
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

    return disconnect_member(*targetClient);
  }
  return false;
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

// * Invites a member
// - id does in the invited vector.
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

// * Promote member into a moderator;
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

Channel::~Channel() {
  std::ostringstream data;
  auto server = this->server.lock();
  data << static_cast<uint32_t>(this->id) << "server destroyed";
  auto packet = create_response(0, DATAKIND::CH_DESTROY, data.str());

  for (WeakClient pointer : this->members) {
    auto client = pointer.lock();
    client->leave_channel(this->id);
    server->threadPool->enqueue(
        [packet, client]() { client->send_packet(packet); });
  }
  std::cout << "channel destroyed" << std::endl;
}

std::string Channel::info() { return ""; }
// * Server cleanup will be done by sending a message to the clients and then
// removing the channel from their channel pool
void Channel::self_destroy(std::string_view reason) {}
