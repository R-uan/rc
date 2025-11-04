#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h> // For O_NONBLOCK
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unistd.h> // For fcntl
#include <vector>

volatile bool flag = true;

struct Client {
  int fd;
  std::thread recvThread;

  inline void send_bytes(uint8_t bytes[], ssize_t size) {
    ssize_t s = ::send(this->fd, bytes, size, 0);
    std::cout << "[OUT] " << s << " bytes" << std::endl;
  }

  Client() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0) {
      std::cerr << "could not open socket" << std::endl;
      exit(1);
    }

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(3000);
    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) <= 0) {
      std::cerr << "invalid address" << std::endl;
      close(fd);
      exit(2);
    }

    if (connect(fd, (sockaddr *)&address, sizeof(address)) < 0) {
      std::cerr << "could not connect to server" << std::endl;
      close(fd);
      exit(3);
    }

    this->fd = fd;
    this->recvThread = std::thread([&, this]() {
      char buffer[4096];
      while (flag) {
        ssize_t read = recv(this->fd, &buffer, sizeof(buffer), 0);
        if (read <= 0) {
          break;
        }
        std::vector<char> incoming;
        incoming.reserve(read);
        std::memcpy(incoming.data(), buffer, read);
      }

      std::cout << "Exiting Recv Thread" << std::endl;
    });
  }

  ~Client() {
    flag = false;
    shutdown(this->fd, SHUT_RDWR);
    close(this->fd);
  }
};

int main() {
  Client bunny;

  uint8_t CONN[]{0x0F, 0x00, 0x00, 0x00,            // size
                 0x01, 0x00, 0x00, 0x00,            // id
                 0x01, 0x00, 0x00, 0x00,            // type
                 'b',  'u',  'n',  'n',  'y', 0x00, // payload
                 0x00};

  bunny.send_bytes(CONN, sizeof(CONN));

  uint8_t JOIN[]{
      0x10, 0x00, 0x00, 0x00, // size
      0x03, 0x00, 0x00, 0x00, // id
      0x04, 0x00, 0x00, 0x00, // type
      0x01,                   // flag
      0x01, 0x00, 0x00, 0x00, // channel id
      0x00,
      0x00 // null
  };

  bunny.send_bytes(JOIN, sizeof(JOIN));
  std::this_thread::sleep_for(std::chrono::seconds(60));
  bunny.~Client();
}
