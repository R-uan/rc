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
        std::cout << "[IN] received `" << read << "` bytes" << std::endl;
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

  uint8_t SVR_CONN[]{0x0F, 0x00, 0x00, 0x00,            // size
                     0x01, 0x00, 0x00, 0x00,            // id
                     0x01, 0x00, 0x00, 0x00,            // type
                     'b',  'u',  'n',  'n',  'y', 0x00, // payload
                     0x00};

  bunny.send_bytes(SVR_CONN, sizeof(SVR_CONN));

  uint8_t CH_JOIN[]{
      0x10, 0x00, 0x00, 0x00, // size
      0x03, 0x00, 0x00, 0x00, // id
      0x04, 0x00, 0x00, 0x00, // type
      0x01,                   // flag
      0x01, 0x00, 0x00, 0x00, // channel id
      0x00,
      0x00 // null
  };

  bunny.send_bytes(CH_JOIN, sizeof(CH_JOIN));
  std::this_thread::sleep_for(std::chrono::seconds(2));

  uint8_t CH_MESSAGE[]{0x13, 0x00, 0x00, 0x00,            // size
                       0x05, 0x00, 0x00, 0x00,            // id
                       0x06, 0x00, 0x00, 0x00,            // type
                       0x01, 0x00, 0x00, 0x00,            //
                       'h',  'e',  'l',  'l',  'o', 0x00, // payload
                       0x00};

  bunny.send_bytes(CH_MESSAGE, sizeof(CH_MESSAGE));
  std::this_thread::sleep_for(std::chrono::seconds(2));

  uint8_t CH_DISCO[]{
      0x0E, 0x00, 0x00, 0x00, //
      0x07, 0x00, 0x00, 0x00, //
      0x05, 0x00, 0x00, 0x00, //
      0x01, 0x00, 0x00, 0x00, //
      0x00,                   //
      0x00,                   //
  };

  bunny.send_bytes(CH_DISCO, sizeof(CH_DISCO));
  std::this_thread::sleep_for(std::chrono::seconds(2));

  uint8_t SRV_DISCO[]{0x0A, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00,
                      0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};

  bunny.send_bytes(SRV_DISCO, sizeof(SRV_DISCO));
  bunny.~Client();
}
