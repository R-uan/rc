#include "server.hpp"
#include <memory>

int main() {
  std::shared_ptr<Server> server(new Server(1000, 50, 20));
  server->listen();
  return 0;
}
