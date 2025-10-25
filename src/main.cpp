#include "server.hpp"
#include <iostream>

int main() {
  RcServer server(1000, 50);
  server.listen();
  std::cout << "Hello World" << std::endl;
  return 0;
}
