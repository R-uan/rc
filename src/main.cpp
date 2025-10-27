#include "server.hpp"

int main() {
  RcServer server(1000, 50);
  server.listen();
  return 0;
}
