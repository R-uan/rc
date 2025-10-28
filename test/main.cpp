#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <fcntl.h> // For O_NONBLOCK
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unistd.h> // For fcntl
int main() {
  const uint8_t CONN[]{0x0F, 0x00, 0x00, 0x00,            // size
                       0x01, 0x00, 0x00, 0x00,            // id
                       0x01, 0x00, 0x00, 0x00,            // type
                       'b',  'u',  'n',  'n',  'y', 0x00, // payload
                       0x00};

  int sock = socket(AF_INET, SOCK_STREAM, 0);

  if (sock < 0) {
    std::cerr << "could not open socket" << std::endl;
    exit(1);
  }

  sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_port = htons(3000);
  if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) <= 0) {
    std::cerr << "invalid address" << std::endl;
    close(sock);
    exit(2);
  }

  if (connect(sock, (sockaddr *)&address, sizeof(address)) < 0) {
    std::cerr << "could not connect to server" << std::endl;
    close(sock);
    exit(3);
  }

  fcntl(sock, F_SETFL, O_NONBLOCK);

  // send connection packet
  ssize_t s = send(sock, CONN, sizeof(CONN), 0);

  // std::this_thread::sleep_for(std::chrono::seconds(2));

  const uint8_t JOIN[]{0x10, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
                       0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x0A, // flag
                       '#',  'g',  'e',  'n',  0x00, 0x00};

  ssize_t ss = send(sock, JOIN, sizeof(JOIN), 0);
  std::this_thread::sleep_for(std::chrono::seconds(5));
}
