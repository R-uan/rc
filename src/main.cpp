#include "server.hpp"
#include <memory>

int main() {
  serversett settings;
  settings.port = 3000;
  settings.maxChannels = 10;
  settings.maxClients = 200;
  settings.dedicatedThreads = 10;

  std::shared_ptr<Server> server(new Server(settings));
  server->listen();
  return 0;
}
