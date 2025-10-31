#include "server.hpp"
#include <memory>

int main() {
  std::shared_ptr<RcServer> server(new RcServer(1000, 50, 20));
  server->listen();
  return 0;
}
