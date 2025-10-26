#pragma once

#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

int i32_from_le(const std::vector<uint8_t> bytes);
std::vector<std::vector<uint8_t>> split_newline(std::vector<uint8_t> &data);

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

enum DATAKIND { CONN = 1, NICK = 2, JOIN = 3, SMSG = 4, INVI = 5, KICK = 6 };
