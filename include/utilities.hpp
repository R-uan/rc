#pragma once

#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

int i32_from_le(const std::vector<uint8_t> bytes);
std::vector<std::vector<uint8_t>> split_newline(std::vector<uint8_t> &data);
enum DATAKIND {
  SVR_CONNECT = 1,
  SVR_DISCONNECT = 2,
  SVR_MESSAGE = 3,
  CH_CONNECT = 4,
  CH_DISCONNECT = 5,
  CH_MESSAGE = 6,
  CH_COMMAND = 7,
  CH_DESTROY = 8
};

struct Response {
  int id{-1};
  int size{-1};
  int type{-1};
  std::vector<char> data{};
};

Response create_response(int id, int type, const std::string_view data);

struct Request {
  int id;
  int type;
  std::vector<uint8_t> payload;

  Request(std::vector<uint8_t> &data) {
    this->id = i32_from_le({data[0], data[1], data[2], data[3]});
    this->type = i32_from_le({data[4], data[5], data[6], data[7]});
    this->payload = std::vector<uint8_t>(&data[8], &data[data.size() - 2]);
    std::cout << "new request of type " << this->type << std::endl;
  }
};
