#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

std::atomic<bool> flag(true);

struct Client {
  int fd;
  std::thread recvThread;
  std::string name;
  std::map<uint32_t, uint32_t> messageCount; // channelId -> message count

  inline void send_bytes(uint8_t bytes[], ssize_t size) {
    ssize_t s = ::send(this->fd, bytes, size, 0);
    std::cout << "[" << name << " OUT] " << s << " bytes" << std::endl;
  }

  Client(const std::string &clientName) : name(clientName) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      std::cerr << "[" << name << "] could not open socket" << std::endl;
      exit(1);
    }
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(3000);
    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) <= 0) {
      std::cerr << "[" << name << "] invalid address" << std::endl;
      close(fd);
      exit(2);
    }
    if (connect(fd, (sockaddr *)&address, sizeof(address)) < 0) {
      std::cerr << "[" << name << "] could not connect to server" << std::endl;
      close(fd);
      exit(3);
    }
    this->fd = fd;
    this->recvThread = std::thread([&, this]() {
      uint8_t buffer[4096];
      while (flag) {
        ssize_t read = recv(this->fd, buffer, sizeof(buffer), 0);
        if (read <= 0) {
          break;
        }

        // Parse incoming packets
        // Expected format: [size(4)] [id(4)] [type(4)] [payload...]
        if (read >= 12) {
          uint32_t type = *reinterpret_cast<uint32_t *>(buffer + 8);

          // CH_MESSAGE = 6
          if (type == 6 && read >= 20) {
            // Payload: [channelId(4)] [clientId(4)] [message...]
            uint32_t channelId = *reinterpret_cast<uint32_t *>(buffer + 12);
            messageCount[channelId]++;
          }
        }

        std::cout << "[" << name << " IN] received `" << read << "` bytes"
                  << std::endl;
      }
      std::cout << "[" << name << "] Exiting Recv Thread" << std::endl;
    });
  }

  void printStats() {
    std::cout << "\n=== [" << name << "] Message Statistics ===" << std::endl;
    uint32_t totalMessages = 0;
    for (const auto &[channelId, count] : messageCount) {
      std::cout << "  Channel " << channelId << ": " << count << " messages"
                << std::endl;
      totalMessages += count;
    }
    std::cout << "  Total: " << totalMessages << " messages" << std::endl;
    std::cout << "======================================\n" << std::endl;
  }

  ~Client() {
    shutdown(this->fd, SHUT_RDWR);
    close(this->fd);
    if (recvThread.joinable()) {
      recvThread.join();
    }
  }
};

void connectToServer(Client &client, const std::string &clientName) {
  uint8_t SVR_CONN[]{0x0F, 0x00, 0x00, 0x00,            // size
                     0x01, 0x00, 0x00, 0x00,            // id
                     0x01, 0x00, 0x00, 0x00,            // type
                     'b',  'u',  'n',  'n',  'y', 0x00, // payload
                     0x00};
  client.send_bytes(SVR_CONN, sizeof(SVR_CONN));
}

void joinChannel(Client &client, uint32_t channelId) {
  uint8_t CH_JOIN[]{0x10,
                    0x00,
                    0x00,
                    0x00, // size
                    0x03,
                    0x00,
                    0x00,
                    0x00, // id
                    0x04,
                    0x00,
                    0x00,
                    0x00, // type
                    0x01, // flag
                    (uint8_t)channelId,
                    0x00,
                    0x00,
                    0x00, // channel id
                    0x00,
                    0x00};
  client.send_bytes(CH_JOIN, sizeof(CH_JOIN));
}

void sendMessage(Client &client, uint32_t channelId,
                 const std::string &message) {
  size_t msgLen = message.length() + 1;
  size_t size = 16 + msgLen + 1;

  std::vector<uint8_t> CH_MESSAGE;
  CH_MESSAGE.push_back(size & 0xFF);
  CH_MESSAGE.push_back((size >> 8) & 0xFF);
  CH_MESSAGE.push_back((size >> 16) & 0xFF);
  CH_MESSAGE.push_back((size >> 24) & 0xFF);

  uint32_t id = 0x05;
  CH_MESSAGE.push_back(id & 0xFF);
  CH_MESSAGE.push_back((id >> 8) & 0xFF);
  CH_MESSAGE.push_back((id >> 16) & 0xFF);
  CH_MESSAGE.push_back((id >> 24) & 0xFF);

  uint32_t type = 0x06;
  CH_MESSAGE.push_back(type & 0xFF);
  CH_MESSAGE.push_back((type >> 8) & 0xFF);
  CH_MESSAGE.push_back((type >> 16) & 0xFF);
  CH_MESSAGE.push_back((type >> 24) & 0xFF);

  CH_MESSAGE.push_back(channelId & 0xFF);
  CH_MESSAGE.push_back((channelId >> 8) & 0xFF);
  CH_MESSAGE.push_back((channelId >> 16) & 0xFF);
  CH_MESSAGE.push_back((channelId >> 24) & 0xFF);

  for (char c : message) {
    CH_MESSAGE.push_back(c);
  }
  CH_MESSAGE.push_back(0x00);
  CH_MESSAGE.push_back(0x00);

  client.send_bytes(CH_MESSAGE.data(), CH_MESSAGE.size());
}

void leaveChannel(Client &client, uint32_t channelId) {
  uint8_t CH_DISCO[]{0x0E,
                     0x00,
                     0x00,
                     0x00, // size
                     0x07,
                     0x00,
                     0x00,
                     0x00, // id
                     0x05,
                     0x00,
                     0x00,
                     0x00, // type
                     (uint8_t)channelId,
                     0x00,
                     0x00,
                     0x00, // channel id
                     0x00,
                     0x00};
  client.send_bytes(CH_DISCO, sizeof(CH_DISCO));
}

void disconnectFromServer(Client &client) {
  uint8_t SRV_DISCO[]{0x0A, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00,
                      0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
  client.send_bytes(SRV_DISCO, sizeof(SRV_DISCO));
}

void clientThread(int clientId, int numChannels, int messagesPerChannel) {
  std::string clientName = "Client_" + std::to_string(clientId);
  Client client(clientName);

  std::cout << "[" << clientName << "] Connected to server" << std::endl;
  connectToServer(client, clientName);
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Join channels
  for (int ch = 1; ch <= numChannels; ch++) {
    joinChannel(client, ch);
    std::cout << "[" << clientName << "] Joined channel " << ch << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::seconds(1));

  // Send messages on each channel
  for (int ch = 1; ch <= numChannels; ch++) {
    for (int msg = 0; msg < messagesPerChannel; msg++) {
      std::string message = clientName + "_ch" + std::to_string(ch) + "_msg" +
                            std::to_string(msg);
      sendMessage(client, ch, message);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

  std::this_thread::sleep_for(std::chrono::seconds(2));

  // Leave channels
  for (int ch = 1; ch <= numChannels; ch++) {
    leaveChannel(client, ch);
    std::cout << "[" << clientName << "] Left channel " << ch << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  disconnectFromServer(client);
  std::cout << "[" << clientName << "] Disconnected" << std::endl;

  // Print statistics
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  client.printStats();
}

int main(int argc, char *argv[]) {
  int numClients = 5;
  int numChannels = 3;
  int messagesPerChannel = 10;

  if (argc > 1)
    numClients = std::atoi(argv[1]);
  if (argc > 2)
    numChannels = std::atoi(argv[2]);
  if (argc > 3)
    messagesPerChannel = std::atoi(argv[3]);

  std::cout << "Starting stress test: " << numClients << " clients, "
            << numChannels << " channels, " << messagesPerChannel
            << " messages per channel" << std::endl;

  std::vector<std::thread> clients;

  for (int i = 0; i < numClients; i++) {
    clients.emplace_back(clientThread, i, numChannels, messagesPerChannel);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  for (auto &t : clients) {
    t.join();
  }

  flag = false;
  std::cout << "\n========== Stress test completed ==========" << std::endl;
  return 0;
}
